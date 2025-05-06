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
const char* ssid     = "aalto open";
const char* password = "";
const char* songURL  = "http://ice1.somafm.com/groovesalad-128-mp3";
const float  gain    = 0.2;

// sensor thresholds & timings
const int   SOUND_THRESHOLD         = 2000;
const unsigned long SOUND_DETECT_MS = 5000;   // 5 s continuous
const unsigned long MOTION_WINDOW_MS= 10000;  // 10 s rolling window
const int   MOTION_THRESHOLD        = 3;      // 3 distinct PIR trips

// lullaby logic
const int   MAX_LULLABIES           = 3;

// pin assignments
const int PIR_PIN      = MOTION_SENSOR_PIN;
const int MIC_PIN      = SOUND_SENSOR_PIN;

//=== Networking & server ===
WebServer server(80);
bool       testMode = false;

//=== Audio playback globals ===
AudioFileSourceHTTPStream *file;
AudioGeneratorMP3         *mp3;
AudioOutputI2S            *out;

//=== Motion / cry state machines & timers ===
bool  lastPirState     = LOW;
int   motionCount      = 0;
unsigned long motionWindowStart = 0;
bool  motionWake       = false;

bool  cryAbove         = false;
unsigned long cryStartTime = 0;

int   lullabyCount     = 0;

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

  // 2) PIR motion detection (rolling-window edge count)
  bool pir = digitalRead(PIR_PIN);
  Serial.printf(" PIR=%d\n", pir);
  int soundV1 = analogRead(MIC_PIN);
  Serial.printf(" Sound=%d\n", soundV1);
  delay(200);

  bool rose = (pir == HIGH && lastPirState == LOW);
  lastPirState = pir;
  if (pir) {
    if (motionWindowStart == 0 || now - motionWindowStart > MOTION_WINDOW_MS) {
      motionWindowStart = now;
      motionCount = 0;
    }
    if (rose) {
      motionCount++;
      Serial.printf(" PIR edge #%d\n", motionCount);
      if (motionCount >= MOTION_THRESHOLD) {
        motionWake = true;
        Serial.println(" >> MOTION: waking detected");
      }
    }
  } else if (motionWindowStart && now - motionWindowStart > MOTION_WINDOW_MS) {
    motionWindowStart = 0;
    motionCount = 0;
  }

  // 3) Cry detection (only if motionWake and not currently playing)
  if (motionWake && !mp3->isRunning()) {
    int soundV = analogRead(MIC_PIN);
    bool soundH = (soundV > SOUND_THRESHOLD);
    Serial.printf(" Sound=%d\n", soundV);
    if (soundH) {
      if (!cryAbove) {
        cryAbove = true;
        cryStartTime = now;
      } else if (now - cryStartTime >= SOUND_DETECT_MS) {
        Serial.println(" >> CRY detected!");
        sendWarningToApp();

        if (lullabyCount < MAX_LULLABIES) {
          lullabyCount++;
          Serial.printf(" Playing lullaby #%d\n", lullabyCount);
          mp3->begin(file, out);
        } else {
          Serial.println(" Max lullabies reached → vibrate phone");
          sendVibrateCommand();
        }

        // reset for next waking episode
        cryAbove = false;
        motionWake = false;
        motionWindowStart = 0;
      }
    } else {
      cryAbove = false;
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

