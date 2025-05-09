#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <AudioFileSourceHTTPStream.h>
#include <AudioGeneratorMP3.h>
#include <AudioOutputI2S.h>
#include <Preferences.h>
#include <NimBLEDevice.h>
#include "esp_camera.h"

#define CAMERA_MODEL_XIAO_ESP32S3  // Has PSRAM
#include "pins_layout.h"            // BCLK_, LRC_, DIN_, MOTION_SENSOR_PIN, SOUND_SENSOR_PIN

//=== User-configurable & stored in NVS ===
Preferences    preferences;
String         ssid, password;

//=== BLE provisioning UUIDs ===
static BLEUUID svcUUID("12345678-1234-5678-1234-56789abcdef0");
static BLEUUID charUUID("abcdef01-1234-5678-1234-56789abcdef0");
bool credsReceived = false;

// forward declaration
void initBleServer();

// BLE characteristic callback to capture SSID,PASSWORD
class CredsCallback : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* chr) override {
    std::string v = chr->getValue();
    int comma = v.find(',');
    if (comma > 0) {
      // Remove old credentials
      preferences.clear();
      // Extract and persist new credentials
      ssid     = String(v.substr(0, comma).c_str());
      password = String(v.substr(comma + 1).c_str());
      preferences.putString("ssid", ssid);
      preferences.putString("pass", password);
      Serial.printf("[BLE] Cleared old creds and stored new SSID=\"%s\", PASS=\"%s\"\n",
                    ssid.c_str(), password.c_str());
      credsReceived = true;
      // Restart to apply updated Wi-Fi settings
      delay(200);
      ESP.restart();
    }
  }
};

//=== Audio and cry-detection globals ===
const char* songURL             = "http://ia600107.us.archive.org/13/items/LullabySong/06-nickelback-lullaby.mp3";
const float  gain               = 0.2;
const int    SOUND_THRESHOLD    = 2000;
const unsigned long PIR_HIGH_MS = 3000;
const unsigned long CRY_WINDOW_MS = 5000;
const int    DIFF_THRESHOLD     = 300;
const int    CRY_COUNT_THRESHOLD = 50;
const int    MAX_LULLABIES      = 3;

// Pin assignments
const int PIR_PIN = MOTION_SENSOR_PIN;
const int MIC_PIN = SOUND_SENSOR_PIN;

// Networking & Test Mode
WebServer server(80);
bool       testMode = false;

// Audio playback objects
AudioFileSourceHTTPStream *file;
AudioGeneratorMP3         *mp3;
AudioOutputI2S            *out;

// Motion / cry state
unsigned long pirHighStart   = 0;
bool         pirTriggered    = false;
unsigned long cryWindowStart = 0;
int          prevSoundV      = 0;
int          crySpikeCount   = 0;
int          lullabyCount    = 0;

//––– Forward declarations for helpers
void startLullaby();
void sendWarningToApp();
void sendVibrateCommand();
void sendTestFeedback(const char* msg);

void setup() {
  Serial.begin(115200);
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  pinMode(MIC_PIN, INPUT);
  pinMode(PIR_PIN, INPUT);

  // 1) Load stored Wi-Fi creds
  preferences.begin("wifi", false);
  ssid     = preferences.getString("ssid", "");
  password = preferences.getString("pass", "");

  // 2) Initialize BLE service (always-on)
  initBleServer();

  // 3) If no creds, wait for provisioning
  if (ssid.length() == 0) {
    Serial.println("[BLE] Waiting for provisioning write...");
    while (!credsReceived) {
      delay(500);
    }
    // restart occurs in callback
    return;
  }

  // 4) Connect to Wi-Fi
  Serial.printf("Connecting to Wi-Fi SSID \"%s\"...\n", ssid.c_str());
  WiFi.begin(ssid.c_str(), password.c_str());
  // Enable modem sleep when both WiFi and BLE active
  WiFi.setSleep(true);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\nWiFi connected: %s\n", WiFi.localIP().toString().c_str());

  // 5) HTTP server for Test Mode
  server.on("/test/on", []() {
    testMode = true;
    server.send(200, "text/plain", "Test mode ON");
    Serial.println("==> Test mode ENABLED");
  });
  server.on("/test/off", []() {
    testMode = false;
    server.send(200, "text/plain", "Test mode OFF");
    Serial.println("==> Test mode DISABLED");
  });
  server.begin();
  Serial.println("HTTP server started");

  // 6) Audio output setup
  out = new AudioOutputI2S();
  out->SetPinout(BCLK_AMPLIFIER_PIN, LRC_AMPLIFIER_PIN, DIN_AMPLIFIER_PIN);
  out->SetOutputModeMono(true);
  out->SetGain(gain);
  file = new AudioFileSourceHTTPStream();
  mp3  = new AudioGeneratorMP3();

  Serial.println("Setup complete. Monitoring sensors...");
}

void loop() {
  unsigned long now = millis();
  server.handleClient();
  if (testMode) {
    if (digitalRead(PIR_PIN)) sendTestFeedback("motion detected");
    if (analogRead(MIC_PIN) > SOUND_THRESHOLD) sendTestFeedback("sound detected");
    delay(100);
    return;
  }

  bool pir = digitalRead(PIR_PIN);
  if (!pirTriggered && !mp3->isRunning()) {
    if (pir) {
      if (pirHighStart == 0) pirHighStart = now;
      else if (now - pirHighStart >= PIR_HIGH_MS) {
        pirTriggered    = true;
        cryWindowStart  = now;
        prevSoundV      = analogRead(MIC_PIN);
        crySpikeCount   = 0;
        Serial.println(">> PIR sustained HIGH → waking");
      }
    } else {
      pirHighStart = 0;
    }
  }

  if (pirTriggered && !mp3->isRunning()) {
    if (now - cryWindowStart <= CRY_WINDOW_MS) {
      int soundV = analogRead(MIC_PIN);
      int diff   = abs(soundV - prevSoundV);
      if (diff > DIFF_THRESHOLD) crySpikeCount++;
      prevSoundV = soundV;
      if (crySpikeCount >= CRY_COUNT_THRESHOLD) {
        Serial.println(">> CRY detected!");
        sendWarningToApp();
        if (lullabyCount < MAX_LULLABIES) {
          lullabyCount++;
          Serial.printf(" Playing lullaby #%d\n", lullabyCount);
          startLullaby();
        } else {
          Serial.println(" Max lullabies reached → vibrate phone");
          sendVibrateCommand();
        }
        pirTriggered = false;
        pirHighStart = 0;
      }
    } else {
      Serial.printf(">> Cry window expired (%d spikes), resetting\n", crySpikeCount);
      pirTriggered = false;
      pirHighStart = 0;
    }
  }

  if (mp3->isRunning()) mp3->loop();
  delay(10);
}

//––– Helpers

void initBleServer() {
  NimBLEDevice::init("BabyMonitor");
  NimBLEDevice::setPower(ESP_PWR_LVL_P3);
  NimBLEServer* srv = NimBLEDevice::createServer();
  NimBLEService* svc = srv->createService(svcUUID);
  NimBLECharacteristic* chr = svc->createCharacteristic(
    charUUID,
    NIMBLE_PROPERTY::WRITE
  );
  chr->setCallbacks(new CredsCallback());
  svc->start();
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(svcUUID);
  adv->setScanResponse(false);
  adv->start();
  Serial.println("[BLE] Service advertising: ready for writes");
}

void startLullaby() {
  file->close();
  delay(50);
  file->open(songURL);
  mp3->begin(file, out);
}

void sendWarningToApp() {
  Serial.println("[APP] Warning: baby crying!");
}

void sendVibrateCommand() {
  Serial.println("[APP] Command: vibrate phone");
}

void sendTestFeedback(const char* msg) {
  Serial.printf("[TEST] %s\n", msg);
}
