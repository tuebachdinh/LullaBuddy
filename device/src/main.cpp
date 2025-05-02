#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <AudioFileSourceHTTPStream.h>
#include <AudioGeneratorMP3.h>
#include <AudioOutputI2S.h>
#include "esp_camera.h"
#include "pins_layout.h"

//=== User-configurable ===
const char* ssid     = "yuuu";
const char* password = "";
const char* songURL  = "http://ice1.somafm.com/groovesalad-128-mp3";
const float  gain    = 0.2;

// sensor thresholds & timings
const int   SOUND_THRESHOLD         = 2000;
const unsigned long SOUND_DETECT_MS = 5000;   // 5s continuous
const unsigned long MOTION_WINDOW_MS= 10000;  // 10s rolling window
const int   MOTION_THRESHOLD        = 3;      // 3 distinct PIR trips

// lullaby logic
const int   MAX_LULLABIES           = 3;

// pin assignments
const int ONOFF_PIN    = 4;   // on/off toggle button
const int PIR_PIN      = MOTION_SENSOR_PIN;
const int MIC_PIN      = SOUND_SENSOR_PIN;
const int ALERT_LED_PIN= 13;  // local LED feedback

//=== Networking & server ===
WebServer server(80);
bool testMode = false;

//=== Audio playback globals ===
AudioFileSourceHTTPStream *file;
AudioGeneratorMP3         *mp3;
AudioOutputI2S            *out;

//=== State machines & timers ===
bool deviceActive = false;

// --- button debouncing ---
bool lastButtonReading = LOW;
bool buttonState       = LOW;
unsigned long lastDebounceTime = 0;
const unsigned long DEBOUNCE_DELAY = 50;

// --- motion detection (rolling window) ---
bool  lastPirState     = LOW;
int   motionCount      = 0;
unsigned long motionWindowStart = 0;
bool  motionWake       = false;

// --- cry detection (continuous) ---
bool  cryAbove         = false;
unsigned long cryStartTime = 0;

// --- lullaby tracking ---
int   lullabyCount     = 0;

// Forward declarations
void sendWarningToApp();
void sendVibrateCommand();
void sendTestFeedback(const char* msg);
void resetAll();

void setup() {
  Serial.begin(115200);
  // sensors
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  pinMode(MIC_PIN, INPUT);
  pinMode(PIR_PIN, INPUT);
  pinMode(ONOFF_PIN, INPUT_PULLUP);
  pinMode(ALERT_LED_PIN, OUTPUT);
  digitalWrite(ALERT_LED_PIN, LOW);

  // WiFi
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
  out = new AudioOutputI2S();
  out->SetPinout(BCLK_AMPLIFIER_PIN, LRC_AMPLIFIER_PIN, DIN_AMPLIFIER_PIN);
  out->SetOutputModeMono(true);
  out->SetGain(gain);
  file = new AudioFileSourceHTTPStream(songURL);
  mp3  = new AudioGeneratorMP3();

  Serial.println("Setup complete. Ready.");
}

void loop() {
  unsigned long now = millis();

  // handle incoming HTTP commands
  server.handleClient();

  // Test Mode: report motion & sound immediately
  if (testMode) {
    bool pir    = digitalRead(PIR_PIN);
    int  soundV = analogRead(MIC_PIN);
    bool soundH = (soundV > SOUND_THRESHOLD);
    if (pir) {
      sendTestFeedback("motion detected");
    }
    if (soundH) {
      sendTestFeedback("sound detected");
    }
    delay(100);
    return;
  }

  //--- handle on/off button ---
  bool reading = digitalRead(ONOFF_PIN);
  if (reading != lastButtonReading) lastDebounceTime = now;
  if (now - lastDebounceTime > DEBOUNCE_DELAY) {
    if (reading != buttonState) {
      buttonState = reading;
      if (buttonState == LOW) {
        deviceActive = !deviceActive;
        Serial.printf("==> TURNED %s\n", deviceActive?"ON":"OFF");
        if (!deviceActive) resetAll();
      }
    }
  }
  lastButtonReading = reading;

  if (!deviceActive) { delay(10); return; }

  //--- PIR motion detection ---
  bool pir = digitalRead(PIR_PIN);
  bool rose = (pir==HIGH && lastPirState==LOW);
  lastPirState = pir;
  if (pir) {
    if (motionWindowStart==0 || now-motionWindowStart>MOTION_WINDOW_MS) {
      motionWindowStart = now; motionCount = 0;
    }
    if (rose) {
      motionCount++;
      Serial.printf(" PIR edge #%d\n", motionCount);
      if (motionCount>=MOTION_THRESHOLD) {
        motionWake = true;
        Serial.println(" >> MOTION: waking");
      }
    }
  } else if (motionWindowStart && now-motionWindowStart>MOTION_WINDOW_MS) {
    motionWindowStart=0; motionCount=0;
  }

  //--- cry detection (only if waking and not playing) ---
  if (motionWake && !mp3->isRunning()) {
    int soundV = analogRead(MIC_PIN);
    bool soundH = (soundV > SOUND_THRESHOLD);
    Serial.printf(" Sound=%d\n", soundV);
    if (soundH) {
      if (!cryAbove) { cryAbove=true; cryStartTime=now; }
      else if (now-cryStartTime>=SOUND_DETECT_MS) {
        Serial.println(" >> CRY detected");
        sendWarningToApp();
        if (lullabyCount<MAX_LULLABIES) {
          lullabyCount++;
          Serial.printf(" Playing lullaby #%d\n", lullabyCount);
          mp3->begin(file, out);
          digitalWrite(ALERT_LED_PIN, HIGH);
        } else {
          Serial.println(" Max lullabies reached → vibrate");
          sendVibrateCommand();
        }
        cryAbove=false; motionWake=false; motionWindowStart=0;
      }
    } else cryAbove=false;
  }

  //--- playback loop ---
  if (mp3->isRunning()) {
    digitalWrite(ALERT_LED_PIN, HIGH);
    mp3->loop();
    if (!mp3->isRunning()) {
      digitalWrite(ALERT_LED_PIN, LOW);
      Serial.println(" Lullaby finished.");
    }
  }

  delay(10);
}

// send a warning to parents' app (push or HTTP)
void sendWarningToApp() {
  Serial.println("[APP] Warning: baby crying!");
  // TODO: HTTP POST or push
}

// send vibrate command to phone via app
void sendVibrateCommand() {
  Serial.println("[APP] Command: vibrate phone");
  // TODO: HTTP call
}

// send feedback in test mode
void sendTestFeedback(const char* msg) {
  Serial.printf("[TEST] %s\n", msg);
  // TODO: HTTP or WebSocket message back to app
}

// reset all states when turning off
void resetAll() {
  motionCount=0; motionWindowStart=0; motionWake=false;
  cryAbove=false; lullabyCount=0;
  if (mp3->isRunning()) mp3->stop();
  digitalWrite(ALERT_LED_PIN, LOW);
}
