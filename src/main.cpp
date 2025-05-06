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

// sensor thresholds & timings
const int SOUND_THRESHOLD        = 2000;     // raw mic threshold (unused now)
const unsigned long PIR_HIGH_MS  = 3000;     // PIR must stay HIGH for 3 s
const unsigned long CRY_WINDOW_MS= 5000;     // listen 5 s after PIR
// new diff-based cry thresholds
const int  DIFF_THRESHOLD        = 300;      // diff > 300 counts as cry spike
const int  CRY_COUNT_THRESHOLD   = 50;      // need 100 such spikes in window

// lullaby logic
const int MAX_LULLABIES         = 3;

// pin assignments
const int PIR_PIN = MOTION_SENSOR_PIN;
const int MIC_PIN = SOUND_SENSOR_PIN;

//=== Networking & server ===
WebServer server(80);
bool       testMode = false;

//=== Audio playback globals ===
AudioFileSourceHTTPStream *file;
AudioGeneratorMP3         *mp3;
AudioOutputI2S            *out;

//=== Motion / cry state ===
unsigned long pirHighStart    = 0;
bool         pirTriggered     = false;
unsigned long cryWindowStart  = 0;
int          prevSoundV       = 0;
int          crySpikeCount    = 0;   // count of diffs exceeding DIFF_THRESHOLD

int          lullabyCount     = 0;

// Forward declarations
void sendWarningToApp();
void sendVibrateCommand();
void sendTestFeedback(const char* msg);

void setup() {
  Serial.begin(115200);

  // sensors
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  pinMode(MIC_PIN, INPUT);
  pinMode(PIR_PIN, INPUT);

  // Wi-Fi
  WiFi.begin(ssid, password);
  WiFi.setSleep(false);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print(".");
  }
  Serial.println("\nWiFi connected");

  // HTTP server routes for Test Mode
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

  // audio output
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

  // 1) HTTP — allow Test Mode toggling
  server.handleClient();
  if (testMode) {
    bool pir    = digitalRead(PIR_PIN);
    int  soundV = analogRead(MIC_PIN);
    bool soundH = (soundV > SOUND_THRESHOLD);
    if (pir)    sendTestFeedback("motion detected");
    if (soundH) sendTestFeedback("sound detected");
    delay(100);
    return;
  }

  // 2) PIR: sustained HIGH for 3 s to trigger wake
  bool pir = digitalRead(PIR_PIN);
  if (!pirTriggered && !mp3->isRunning()) {
    if (pir) {
      if (pirHighStart == 0) {
        pirHighStart = now;
      } else if (now - pirHighStart >= PIR_HIGH_MS) {
        pirTriggered      = true;
        cryWindowStart    = now;
        prevSoundV        = analogRead(MIC_PIN);
        crySpikeCount     = 0;
        Serial.println(" >> PIR: sustained HIGH → waking");
      }
    } else {
      pirHighStart = 0;
    }
  }

  // 3) Cry detection: count spikes in 5 s window
  if (pirTriggered && !mp3->isRunning()) {
    if (now - cryWindowStart <= CRY_WINDOW_MS) {
      int soundV = analogRead(MIC_PIN);
      int diff   = abs(soundV - prevSoundV);
      if (diff > DIFF_THRESHOLD) {
        crySpikeCount++;
      }
      if (diff > 300) {Serial.printf(" Sound diff=%d (spikes=%d/%d)\n", diff, crySpikeCount, CRY_COUNT_THRESHOLD);}

      if (crySpikeCount >= CRY_COUNT_THRESHOLD) {
        Serial.println(" >> CRY detected (spike count threshold reached)!");
        sendWarningToApp();

        if (lullabyCount < MAX_LULLABIES) {
          lullabyCount++;
          Serial.printf(" Playing lullaby #%d\n", lullabyCount);
          mp3->begin(file, out);
        } else {
          Serial.println(" Max lullabies reached → vibrate phone");
          sendVibrateCommand();
        }

        // reset for next cycle
        pirTriggered   = false;
        pirHighStart   = 0;
        crySpikeCount  = 0;
      }
      prevSoundV = soundV;
    } else {
      Serial.printf(" >> Cry window expired (%d spikes), resetting\n", crySpikeCount);
      pirTriggered   = false;
      pirHighStart   = 0;
      crySpikeCount  = 0;
    }
  }

  // 4) Playback loop
  if (mp3->isRunning()) {
    mp3->loop();
  }

  delay(10);
}

// send a warning to parents' app (push or HTTP)
void sendWarningToApp() {
  Serial.println("[APP] Warning: baby crying!");
  // TODO: implement HTTP POST or push notification
}

// send vibrate command to phone via app
void sendVibrateCommand() {
  Serial.println("[APP] Command: vibrate phone");
  // TODO: implement HTTP call to trigger vibration
}

// send feedback in test mode
void sendTestFeedback(const char* msg) {
  Serial.printf("[TEST] %s\n", msg);
  // TODO: send via HTTP or WebSocket to your app
}

