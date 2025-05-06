#include <Arduino.h>
#include <WiFi.h>
#include <AudioFileSourceHTTPStream.h>
#include <AudioGeneratorMP3.h>
#include <AudioOutputI2S.h>
#include "esp_camera.h"
#define CAMERA_MODEL_XIAO_ESP32S3 // Has PSRAM
#include "pins_layout.h"
#include <WebServer.h>

//=== User-configurable ===
const char* ssid     = "yuuu";
const char* password = "servin022";
const char* songURL  = "https://ia600107.us.archive.org/13/items/LullabySong/06-nickelback-lullaby.mp3";
const float  gain    = 0.2;

//=== Battery monitor ===
// ADC parameters for ESP32S3:
const float ADC_REF      = 3.3f;  
const int   ADC_RES      = 4095;  
// Divider ratio = (R1 + R2) / R2. E.g. 100 kΩ/100 kΩ → 2.0
const float R_DIVIDER    = 2.0f;  

// Map LiPo voltage (3.0–4.2 V) → %  
float voltageToPercent(float v) {
  if (v >= 4.20f) return 100.0f;
  if (v <= 3.00f) return   0.0f;
  return (v - 3.00f) / (4.20f - 3.00f) * 100.0f;
}

//=== sensor thresholds & timings ===
const int SOUND_THRESHOLD        = 2000;
const unsigned long PIR_HIGH_MS  = 3000;
const unsigned long CRY_WINDOW_MS= 5000;
const int  DIFF_THRESHOLD        = 300;
const int  CRY_COUNT_THRESHOLD   = 50;
const int MAX_LULLABIES          = 3;

//=== pins ===
const int PIR_PIN = MOTION_SENSOR_PIN;
const int MIC_PIN = SOUND_SENSOR_PIN;

//=== Networking ===
WebServer server(80);
bool       testMode = false;

//=== Audio globals ===
AudioFileSourceHTTPStream *file;
AudioGeneratorMP3         *mp3;
AudioOutputI2S            *out;

//=== State ===
unsigned long pirHighStart   = 0, cryWindowStart = 0;
bool         pirTriggered    = false;
int          prevSoundV      = 0, crySpikeCount = 0;
int          lullabyCount    = 0;

void sendWarningToApp();
void sendVibrateCommand();
void sendTestFeedback(const char* msg);

void setup() {
  Serial.begin(115200);

  // ADC setup
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  pinMode(MIC_PIN, INPUT);
  pinMode(PIR_PIN, INPUT);
  pinMode(BAT_ADC_PIN, INPUT);

  // Wi-Fi
  WiFi.begin(ssid, password);
  WiFi.setSleep(false);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print(".");
  }
  Serial.println("\nWiFi connected");

  // HTTP routes
  server.on("/test/on",  [](){ testMode=true;  server.send(200,"text/plain","Test ON");  Serial.println("Test ON"); });
  server.on("/test/off", [](){ testMode=false; server.send(200,"text/plain","Test OFF"); Serial.println("Test OFF"); });
  server.begin();

  // Audio output
  out  = new AudioOutputI2S();
  out->SetPinout(BCLK_AMPLIFIER_PIN, LRC_AMPLIFIER_PIN, DIN_AMPLIFIER_PIN);
  out->SetOutputModeMono(true);
  out->SetGain(gain);
  file = new AudioFileSourceHTTPStream(songURL);
  mp3  = new AudioGeneratorMP3();

  Serial.println("Setup complete. Monitoring sensors...");
}

void loop() {
  unsigned long now = millis();

  // — Test mode feedback —
  server.handleClient();
  if (testMode) {
    if (digitalRead(PIR_PIN))    sendTestFeedback("motion detected");
    if (analogRead(MIC_PIN) > SOUND_THRESHOLD) sendTestFeedback("sound detected");
    delay(100);
    return;
  }

  // — PIR wake logic —
  bool pir = digitalRead(PIR_PIN);
  if (!pirTriggered && !mp3->isRunning()) {
    if (pir) {
      if (pirHighStart==0) pirHighStart = now;
      else if (now - pirHighStart >= PIR_HIGH_MS) {
        pirTriggered   = true;
        cryWindowStart = now;
        prevSoundV     = analogRead(MIC_PIN);
        crySpikeCount  = 0;
        Serial.println(">> PIR sustained HIGH → waking");
      }
    } else pirHighStart = 0;
  }

  // — Cry detection & lullaby trigger —
  if (pirTriggered && !mp3->isRunning()) {
    if (now - cryWindowStart <= CRY_WINDOW_MS) {
      int soundV = analogRead(MIC_PIN);
      int diff   = abs(soundV - prevSoundV);
      if (diff > DIFF_THRESHOLD) crySpikeCount++;

      // if (diff > 300) {Serial.printf(" Sound diff=%d (spikes=%d/%d)\n", diff, crySpikeCount, CRY_COUNT_THRESHOLD);}

      if (crySpikeCount >= CRY_COUNT_THRESHOLD) {
        Serial.println(">> CRY detected!");
        sendWarningToApp();
        if (lullabyCount < MAX_LULLABIES) {
          lullabyCount++;
          Serial.printf(" Playing lullaby #%d\n", lullabyCount);
          mp3->begin(file, out);
        } else {
          Serial.println(" Max lullabies reached → vibrate phone");
          sendVibrateCommand();
        }
        pirTriggered = false;
        pirHighStart = 0;
      }
      prevSoundV = soundV;
    } else {
      Serial.printf(">> Cry window expired (%d spikes)\n", crySpikeCount);
      pirTriggered = false;
      pirHighStart = 0;
    }
  }

  // — Audio playback loop —
  if (mp3->isRunning()) {
    mp3->loop();
  }

  // — Battery check every 60 s —
  static unsigned long lastBatt = 0;
  if (now - lastBatt >= 60000) {
    lastBatt = now;
    int raw   = analogRead(BAT_ADC_PIN);
    float vDiv= raw / (float)ADC_RES * ADC_REF;
    float vBat= vDiv * R_DIVIDER;
    float pct = voltageToPercent(vBat);
    Serial.printf("Battery: %.2f V (%.0f%%)\n", vBat, pct);
  }

  delay(10);
}

void sendWarningToApp() {
  Serial.println("[APP] Warning: baby crying!");
  // TODO: implement your push/HTTP here
}

void sendVibrateCommand() {
  Serial.println("[APP] Command: vibrate phone");
  // TODO: implement your HTTP here
}

void sendTestFeedback(const char* msg) {
  Serial.printf("[TEST] %s\n", msg);
  // TODO: send via WebSocket/HTTP to your app
}
