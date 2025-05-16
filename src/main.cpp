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
const float          gain               = 0.2;
const int            SOUND_THRESHOLD    = 2000;
const unsigned long  PIR_HIGH_MS        = 3000;
const unsigned long  CRY_WINDOW_MS      = 5000;
const int            DIFF_THRESHOLD     = 300;
const int            CRY_COUNT_THRESHOLD= 50;
const int            MAX_LULLABIES      = 3;
const size_t         BUF_SIZE           = 256 * 1024;

const int PIR_PIN = MOTION_SENSOR_PIN;
const int MIC_PIN = SOUND_SENSOR_PIN;

WebServer server(80);
bool       testMode     = false;
int        lullabyCount = 0;

AudioFileSourceHTTPStream *file   = nullptr;
AudioFileSourceBuffer     *buffer = nullptr;
AudioGeneratorMP3         *mp3    = nullptr;
AudioOutputI2S            *out    = nullptr;

// Forward declarations
void startLullaby();
bool sendPattern(const char*);
void sendWarningToApp();
void sendVibrateCommand();
void sendTestFeedback(const char*);

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
  Serial.printf("→ Got API token: %s\n", apiToken.c_str());
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

  if (code == HTTP_CODE_OK) {
    Serial.printf("→ Sent pattern \"%s\"\n", patternType);
    return true;
  } else {
    Serial.printf("Pattern send failed: %d\n", code);
    return false;
  }
}

bool playCloudSong(int soundId) {
  String url = String("https://") + API_HOST
               + "/api/devices/" + String(DEVICE_ID)
               + "/sounds/" + soundId + "/download";

  // library patched to add Authorization header in open()
  auto* httpSource = new AudioFileSourceHTTPStream();
  httpSource->open(url.c_str());

  delete buffer;
  buffer = new AudioFileSourceBuffer(httpSource, BUF_SIZE);

  if (mp3->isRunning()) mp3->stop();
  mp3->begin(buffer, out);
  return true;
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
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  pinMode(MIC_PIN, INPUT);
  pinMode(PIR_PIN, INPUT);

  // Load stored Wi-Fi creds
  preferences.begin("wifi", false);
  ssid     = preferences.getString("ssid", "");
  password = preferences.getString("pass", "");
  if (ssid == "aalto open") { preferences.clear(); ssid=""; password=""; }

  // BLE provisioning
  initBleServer();
  if (ssid.length() == 0) {
    Serial.println("[BLE] Waiting for provisioning…");
    while (!credsReceived) delay(500);
    return;
  }

  // Connect Wi-Fi
  Serial.printf("Connecting to Wi-Fi \"%s\"…\n", ssid.c_str());
  WiFi.begin(ssid.c_str(), password.c_str());
  WiFi.setSleep(true);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
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
    if (digitalRead(PIR_PIN)) sendTestFeedback("motion");
    if (analogRead(MIC_PIN) > SOUND_THRESHOLD) sendTestFeedback("sound");
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
        Serial.println(">> PIR HIGH → baby awake");
        sendPattern("awake");
      }
    } else pirHighStart = 0;
  }

  if (pirTriggered && !mp3->isRunning()) {
    if (now - cryWindowStart <= CRY_WINDOW_MS) {
      int v = analogRead(MIC_PIN);
      if (abs(v - prevSound) > DIFF_THRESHOLD) crySpikes++;
      prevSound = v;
      if (crySpikes >= CRY_COUNT_THRESHOLD) {
        Serial.println(">> Cry detected!");
        sendWarningToApp();
        sendPattern("awake");
        if (lullabyCount < MAX_LULLABIES) {
          lullabyCount++;
          Serial.printf(" Playing lullaby #%d\n", lullabyCount);
          startLullaby();
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

void startLullaby() {
  mp3->begin(buffer, out);
}

void sendWarningToApp()   { Serial.println("[APP] Warning: baby crying!"); }

void sendVibrateCommand() { Serial.println("[APP] Command: vibrate phone"); }

void sendTestFeedback(const char* msg) {
  Serial.printf("[TEST] %s\n", msg);
}
