#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <AudioFileSourceHTTPStream.h>
#include <AudioFileSourceBuffer.h>
#include <AudioGeneratorMP3.h>
#include <AudioOutputI2S.h>
#include <Preferences.h>
#include <NimBLEDevice.h>
#include <time.h>
#include "esp_camera.h"

#define CAMERA_MODEL_XIAO_ESP32S3
#include "pins_layout.h"  // BCLK_, LRC_, DIN_, MOTION_SENSOR_PIN, SOUND_SENSOR_PIN
#include "camera_index.h"

//=== Wi-Fi & provisioning ===
Preferences    preferences;
String         ssid, password;
bool           credsReceived = false;
static BLEUUID svcUUID("12345678-1234-5678-1234-56789abcdef0");
static BLEUUID charUUID("abcdef01-1234-5678-1234-56789abcdef0");
void initBleServer();

//=== Cloud API config ===
static const char* API_HOST  = "theta.proto.aalto.fi";
static const int   DEVICE_ID = 1;
static const char* API_USER  = "demo";      // replace securely
static const char* API_PASS  = "demodemo";  // replace securely
String             apiToken;

//=== Audio & cry globals ===
const float ADC_REF      = 3.3f;  
const int   ADC_RES      = 4095;  
// Divider ratio = (R1 + R2) / R2. E.g. 100 kΩ/100 kΩ → 2.0
const float R_DIVIDER    = 2.0f;  
const float          gain               = 0.2;
const int            SOUND_THRESHOLD    = 2000;
const unsigned long  PIR_HIGH_MS        = 3000;
const unsigned long  CRY_WINDOW_MS      = 5000;
const int            DIFF_THRESHOLD     = 300;
const int            CRY_COUNT_THRESHOLD= 50;
const int            MAX_LULLABIES      = 3;
const size_t         BUF_SIZE           = 256 * 1024;
const char* songURL   = "http://ia600107.us.archive.org/13/items/LullabySong/06-nickelback-lullaby.mp3";

const int PIR_PIN = MOTION_SENSOR_PIN;
const int MIC_PIN = SOUND_SENSOR_PIN;

WebServer server(80);
bool       testMode     = false;
int        lullabyCount = 0;

AudioFileSourceHTTPStream *file   = nullptr;
AudioFileSourceBuffer     *buffer = nullptr;
AudioGeneratorMP3         *mp3    = nullptr;
AudioOutputI2S            *out    = nullptr;

// Map LiPo voltage (3.0–4.2 V) → %  
float voltageToPercent(float v) {
  if (v >= 4.20f) return 100.0f;
  if (v <= 3.00f) return   0.0f;
  return (v - 3.00f) / (4.20f - 3.00f) * 100.0f;
}

//=== Cloud functions ===
bool apiLogin() {
  WiFiClientSecure* client = new WiFiClientSecure();
  client->setInsecure(); // TODO: load real CA

  HTTPClient https;
  String url = String("https://") + API_HOST + "/api/users/login";
  https.begin(*client, url);
  https.addHeader("Content-Type", "application/json");

  StaticJsonDocument<128> body;
  body["username"] = API_USER;
  body["password"] = API_PASS;
  String payload;
  serializeJson(body, payload);

  int code = https.POST(payload);
  if (code != HTTP_CODE_OK) {
    Serial.printf("API login failed: %d\n", code);
    https.end(); delete client;
    return false;
  }

  StaticJsonDocument<256> resp;
  DeserializationError err = deserializeJson(resp, https.getString());
  https.end(); delete client;
  if (err) {
    Serial.println("JSON parse error on login");
    return false;
  }

  apiToken = resp["token"].as<String>();
  Serial.printf("→ Success Login: Got API token: %s\n", apiToken.c_str());
  return true;
}

bool sendPattern(const char* patternType) {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient https;
  String url = String("https://") + API_HOST
               + "/api/devices/" + String(DEVICE_ID) + "/patterns";
  https.begin(client, url);
  https.addHeader("Authorization", "Bearer " + apiToken);
  https.addHeader("Content-Type", "application/json");

  StaticJsonDocument<128> doc;
  char buf[32];
  time_t now = time(nullptr);
  struct tm *g = gmtime(&now);
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", g);
  doc["timestamp"]   = buf;
  doc["patternType"] = patternType;

  String body;
  serializeJson(doc, body);
  int code = https.PUT(body);
  https.end();

  if (code >= 200 && code < 300) {
    Serial.printf("→ Sent pattern \"%s\" (HTTP %d)\n", patternType, code);
    return true;
  } else {
    Serial.printf("Pattern send failed: HTTP %d\n", code);
    return false;
  }
}

bool playCloudSong() {
  // ————— 1) tear down any prior playback —————
  if (mp3->isRunning()) {
    mp3->stop();
    delay(10);
  }
  if (buffer) {
    buffer->close();
    delete buffer;
    buffer = nullptr;
  }
  if (file) {
    file->close();
    delete file;
    file = nullptr;
  }

  // ————— 2) build the "active sound" URL —————
  String url = String("https://") + API_HOST
               + "/api/devices/" + String(DEVICE_ID)
               + "/sounds/active";

  Serial.printf("→ Fetching active sound from: %s\n", url.c_str());

  // ————— 3) open the HTTP stream directly —————
  file = new AudioFileSourceHTTPStream();
  if (!file->open(url.c_str())) {
    // file->open() will return false on 404 or any non-200
    Serial.println("→ No active sound or HTTP error");
    delete file;
    file = nullptr;
    return false;
  }

  // ————— 4) wrap & kick off the decoder —————
  buffer = new AudioFileSourceBuffer(file, BUF_SIZE);
  mp3->begin(buffer, out);

  return true;
}

void startLullaby() {
  mp3->begin(buffer, out);
}

bool sendCommand(const char* cmd) {
  WiFiClientSecure client;
  client.setInsecure();           // or setInsecure()
  HTTPClient http;
  String url = String("https://") + API_HOST
               + "/api/devices/" + DEVICE_ID + "/commands";
  http.begin(client, url);
  http.addHeader("Authorization","Bearer "+apiToken);
  http.addHeader("Content-Type","application/json");

  // build payload
  StaticJsonDocument<64> doc;
  doc["command"] = cmd;
  String body;
  serializeJson(doc, body);

  int code = http.PUT(body);
  http.end();
  if (code >= 200 && code < 300) {
    Serial.printf("→ Sent command \"%s\" (HTTP %d)\n", cmd, code);
    return true;
  } else {
    Serial.printf("Command send failed: HTTP %d\n", code);
    return false;
  }
  return (code >=200 && code<300);
 
}

void sendWarningToApp()   { sendCommand("notification"); }
void sendVibrateCommand() { sendCommand("vibrate"); }
void sendMotionFeedback() { sendCommand("motion_detected"); }
void sendSoundFeedback() { sendCommand("sound_detected"); }

// bool sendImageToCloud() {
//   // 1) snap a photo
//   camera_fb_t *fb = esp_camera_fb_get();
//   if (!fb) {
//     Serial.println("📸 Camera capture failed");
//     return false;
//   }

//   // 2) prepare secure client
//   WiFiClientSecure client;
//   client.setInsecure();  // or use client.setCACert(yourCACert);

//   // 3) build URL (change “image” to your real endpoint)
//   String url = String("https://") + API_HOST
//                + "/api/devices/" + String(DEVICE_ID)
//                + "/image";

//   // 4) POST the JPEG buffer
//   HTTPClient http;
//   http.begin(client, url);
//   http.addHeader("Authorization", "Bearer " + apiToken);
//   http.addHeader("Content-Type", "image/jpeg");

//   int code = http.POST(fb->buf, fb->len);
//   if (code >= 200 && code < 300) {
//     Serial.printf("✅ Image upload OK (HTTP %d)\n", code);
//     http.end();
//     esp_camera_fb_return(fb);
//     return true;
//   } else {
//     Serial.printf("❌ Image upload failed: HTTP %d\n", code);
//     Serial.println(http.getString());
//     http.end();
//     esp_camera_fb_return(fb);
//     return false;
//   }
// }

bool sendIpToCloud(const String& ip) {
  // 1) prepare secure client
  WiFiClientSecure client;
  client.setInsecure();  // or load your CA

  // 2) build URL
  String url = String("https://") + API_HOST
               + "/api/devices/" + String(DEVICE_ID)
               + "/ip";

  // 3) start PUT
  HTTPClient https;
  https.begin(client, url);
  https.addHeader("Authorization", "Bearer " + apiToken);
  https.addHeader("Content-Type", "application/json");

  // 4) build payload
  StaticJsonDocument<64> doc;
  doc["IPaddress"] = ip;                   // <-- as per cloud spec
  String payload;
  serializeJson(doc, payload);

  // 5) send & clean up
  int code = https.PUT(payload);
  https.end();

  // 6) report result
  if (code >= 200 && code < 300) {
    Serial.printf("→ Sent IP “%s” OK (HTTP %d)\n", ip.c_str(), code);
    return true;
  } else {
    Serial.printf("→ Failed to send IP (HTTP %d)\n", code);
    return false;
  }
}

// BLE provisioning callback
class CredsCallback : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* chr) override {
    std::string v = chr->getValue();
    int comma = v.find(',');
    if (comma > 0) {
      preferences.clear();
      ssid     = String(v.substr(0, comma).c_str());
      password = String(v.substr(comma+1).c_str());
      preferences.putString("ssid", ssid);
      preferences.putString("pass", password);
      Serial.printf("[BLE] Stored new SSID=\"%s\"\n", ssid.c_str());
      credsReceived = true;
      delay(200);
      ESP.restart();
    }
  }
};

void setup() {
  Serial.begin(115200);
  
  // camera_config_t config;
  // config.ledc_channel = LEDC_CHANNEL_0;
  // config.ledc_timer = LEDC_TIMER_0;
  // config.pin_d0 = Y2_GPIO_NUM;
  // config.pin_d1 = Y3_GPIO_NUM;
  // config.pin_d2 = Y4_GPIO_NUM;
  // config.pin_d3 = Y5_GPIO_NUM;
  // config.pin_d4 = Y6_GPIO_NUM;
  // config.pin_d5 = Y7_GPIO_NUM;
  // config.pin_d6 = Y8_GPIO_NUM;
  // config.pin_d7 = Y9_GPIO_NUM;
  // config.pin_xclk = XCLK_GPIO_NUM;
  // config.pin_pclk = PCLK_GPIO_NUM;
  // config.pin_vsync = VSYNC_GPIO_NUM;
  // config.pin_href = HREF_GPIO_NUM;
  // config.pin_sccb_sda = SIOD_GPIO_NUM;
  // config.pin_sccb_scl = SIOC_GPIO_NUM;
  // config.pin_pwdn = PWDN_GPIO_NUM;
  // config.pin_reset = RESET_GPIO_NUM;
  // config.xclk_freq_hz = 20000000;
  // config.frame_size = FRAMESIZE_UXGA;
  // config.pixel_format = PIXFORMAT_JPEG;  // for streaming
  // //config.pixel_format = PIXFORMAT_RGB565; // for face detection/recognition
  // config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  // config.fb_location = CAMERA_FB_IN_PSRAM;
  // config.jpeg_quality = 12;
  // config.fb_count = 1;

  // if (esp_camera_init(&config) != ESP_OK) {
  //   Serial.println("Camera init failed");
  //   while(true);
  // }

  
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  pinMode(MIC_PIN, INPUT);
  pinMode(PIR_PIN, INPUT);

  //Load stored Wi-Fi creds
  preferences.begin("wifi", false);
  ssid     = preferences.getString("ssid", "");
  password = preferences.getString("pass", "");
  Serial.printf("Stored Wi-Fi creds: \"%s\" / \"%s\"\n", ssid.c_str(), password.c_str());
  
  // if (ssid == "aalto open") { preferences.clear(); ssid=""; password=""; }

  // // BLE provisioning
  // initBleServer();
  // if (ssid.length() == 0) {
  //   Serial.println("[BLE] Waiting for provisioning…");
  //   while (!credsReceived) {
  //     delay(500);
  //     Serial.print(",");
  //   }
  //   return;
  // }

  ssid = "aalto open";
  password = "";

  // Connect Wi-Fi
  Serial.printf("Connecting to Wi-Fi \"%s\"…\n", ssid.c_str());
  WiFi.begin(ssid.c_str(), password.c_str());
  WiFi.setSleep(true);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print(".");}
  Serial.printf("\nWi-Fi up, IP=%s\n", WiFi.localIP().toString().c_str());


  // NTP time sync
  configTime(0, 0, "pool.ntp.org", "time.google.com");
  Serial.print("Waiting for time");
  while (time(nullptr) < 24*3600) { delay(500); Serial.print("."); }
  Serial.println(" done.");

  // Cloud login
  if (!apiLogin()) Serial.println("Cloud auth failed");

  // Test HTTP server
  server.on("/test/on",  [](){ testMode=true;  server.send(200,"text/plain","ON"); });
  server.on("/test/off", [](){ testMode=false; server.send(200,"text/plain","OFF"); });
  server.begin();

  // Send IP to cloud
  String myIp = WiFi.localIP().toString();
  if (!sendIpToCloud(myIp)) {
    Serial.println("Error: could not register IP with cloud");
  }

  // Audio init
  out = new AudioOutputI2S();
  out->SetPinout(BCLK_AMPLIFIER_PIN, LRC_AMPLIFIER_PIN, DIN_AMPLIFIER_PIN);
  out->SetOutputModeMono(true);
  out->SetGain(gain);

  file   = new AudioFileSourceHTTPStream();
  buffer = new AudioFileSourceBuffer(file, BUF_SIZE);
  mp3    = new AudioGeneratorMP3();

  Serial.println("Setup complete; monitoring...");
}

void loop() {
  server.handleClient();
  if (testMode) {
    if (digitalRead(PIR_PIN)) sendMotionFeedback();
    if (analogRead(MIC_PIN) > SOUND_THRESHOLD) sendSoundFeedback();
    delay(100);
    return;
  }

  unsigned long now = millis();
  static unsigned long pirHighStart = 0;
  static bool pirTriggered = false;
  static unsigned long cryWindowStart = 0;
  static int prevSound = 0, crySpikes = 0;

  bool pir = digitalRead(PIR_PIN);
  if (!pirTriggered && !mp3->isRunning()) {
    if (pir) {
      if (pirHighStart == 0) pirHighStart = now;
      else if (now - pirHighStart >= PIR_HIGH_MS) {
        pirTriggered   = true;
        cryWindowStart = now;
        prevSound      = analogRead(MIC_PIN);
        crySpikes      = 0;
        Serial.println(">> PIR HIGH → baby has some motions");
        //sendPattern("move");
        sendWarningToApp();
        sendVibrateCommand();
        sendPattern("sleep");
        sendPattern("awake");
        //playCloudSong();
      }
    } else {
      pirHighStart = 0;
      //sendPattern("sleep");
    }
  }

  if (pirTriggered && !mp3->isRunning()) {
    if (now - cryWindowStart <= CRY_WINDOW_MS) {
      int v = analogRead(MIC_PIN);
      if (abs(v - prevSound) > DIFF_THRESHOLD) crySpikes++;
      prevSound = v;
      if (crySpikes >= CRY_COUNT_THRESHOLD) {
        Serial.println(">> Cry detected! Baby is awake");
        sendWarningToApp();
        sendPattern("awake");
        //sendImageToCloud();

        if (lullabyCount < MAX_LULLABIES) {
          lullabyCount++;
          Serial.printf(" Playing lullaby #%d\n", lullabyCount);
          //startLullaby();
          playCloudSong();
        } else {
          Serial.println(" Max lullabies → vibrate");
          sendVibrateCommand();
        }
        pirTriggered = false;
        pirHighStart = 0;
      }
    } else {
      Serial.printf(">> Cry window expired (%d), baby sleeping\n", crySpikes);
      sendPattern("sleep");
      pirTriggered = false;
      pirHighStart = 0;
    }
  }

  static unsigned long lastBatt = 0;
  if (now - lastBatt >= 60000) {
    lastBatt = now;
    int raw   = analogRead(BAT_ADC_PIN);
    float vDiv= raw / (float)ADC_RES * ADC_REF;
    float vBat= vDiv * R_DIVIDER;
    float pct = voltageToPercent(vBat);
    Serial.printf("Battery: %.2f V (%.0f%%)\n", vBat, pct);
  }

  if (mp3->isRunning()) mp3->loop();
  delay(10);
}

// Helpers
void initBleServer() {
  NimBLEDevice::init("BabyMonitor");
  NimBLEDevice::setPower(ESP_PWR_LVL_P3);
  NimBLEServer* srv = NimBLEDevice::createServer();
  NimBLEService* svc= srv->createService(svcUUID);
  auto* chr = svc->createCharacteristic(charUUID, NIMBLE_PROPERTY::WRITE);
  chr->setCallbacks(new CredsCallback());
  svc->start();
  NimBLEDevice::getAdvertising()->addServiceUUID(svcUUID);
  NimBLEDevice::getAdvertising()->start();
  Serial.println("[BLE] advertising…");
}


