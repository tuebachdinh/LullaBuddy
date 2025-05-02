#include <Arduino.h>
#include <WiFi.h>
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
const int   SOUND_THRESHOLD        = 2000;
const unsigned long SOUND_DETECT_MS = 5000;  // 5 s continuous to count as “cry”
const unsigned long MOTION_WINDOW_MS = 10000; // 10 s rolling window
const int   MOTION_THRESHOLD       = 3;      // 3 distinct PIR trips in window

// lullaby logic
const int   MAX_LULLABIES          = 3;

// pin assignments
const int ONOFF_PIN     = 4;   // your on/off toggle button
const int PIR_PIN       = MOTION_SENSOR_PIN;
const int MIC_PIN       = SOUND_SENSOR_PIN;
const int ALERT_LED_PIN = 13;  // feedback local LED (optional)

//=== WiFi & audio globals ===
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

// --- motion detection (rolling‐window) ---
bool  lastPirState    = LOW;
int   motionCount     = 0;
unsigned long motionWindowStart = 0;
bool  motionWake      = false;

// --- cry detection (continuous) ---
bool  cryAbove        = false;
unsigned long cryStartTime = 0;

// --- lullaby tracking ---
int   lullabyCount    = 0;

// forward declarations
void sendWarningToApp();
void sendVibrateCommand();
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

  // audio output
  out = new AudioOutputI2S();
  out->SetPinout(BCLK_AMPLIFIER_PIN, LRC_AMPLIFIER_PIN, DIN_AMPLIFIER_PIN);
  out->SetOutputModeMono(true);
  out->SetGain(gain);
  file = new AudioFileSourceHTTPStream(songURL);
  mp3  = new AudioGeneratorMP3();

  Serial.println("Setup complete. Waiting for ON/OFF...");
}

void loop() {
  //—— handle on/off button ——
  bool reading = digitalRead(ONOFF_PIN);
  if (reading != lastButtonReading) {
    lastDebounceTime = millis();
  }
  if (millis() - lastDebounceTime > DEBOUNCE_DELAY) {
    if (reading != buttonState) {
      buttonState = reading;
      // toggle on falling edge of switch (assuming pull-up)
      if (buttonState == LOW) {
        deviceActive = !deviceActive;
        if (!deviceActive) {
          Serial.println("==> TURNED OFF");
          resetAll();
        } else {
          Serial.println("==> TURNED ON");
        }
      }
    }
  }
  lastButtonReading = reading;

  if (!deviceActive) {
    delay(10);
    return;
  }

  unsigned long now = millis();

  //—— PIR motion detection (rolling window) ——
  bool pir = digitalRead(PIR_PIN);
  // edge detect
  bool rose = (pir == HIGH && lastPirState == LOW);
  lastPirState = pir;

  if (pir) {
    // start/reset window
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
    // expire window
    motionWindowStart = 0;
    motionCount = 0;
  }

  // once motionWake is true, we move on to cry detection...
  if (motionWake) {
    //—— cry detection — skip while playing lullaby ——
    if (!mp3->isRunning()) {
      int soundValue = analogRead(MIC_PIN);
      bool soundHigh = (soundValue > SOUND_THRESHOLD);
      Serial.printf(" Sound=%d\n", soundValue);

      if (soundHigh) {
        if (!cryAbove) {
          cryAbove = true;
          cryStartTime = now;
        } else if (now - cryStartTime >= SOUND_DETECT_MS) {
          // crying detected
          Serial.println(" >> CRY: detected!");
          sendWarningToApp();

          if (lullabyCount < MAX_LULLABIES) {
            lullabyCount++;
            Serial.printf(" Playing lullaby #%d\n", lullabyCount);
            mp3->begin(file, out);
            digitalWrite(ALERT_LED_PIN, HIGH);
          } else {
            // after finishing 3, send vibrate instead
            Serial.println(" Max lullabies reached → sending vibration");
            sendVibrateCommand();
          }
          // reset cry detector until next motion
          cryAbove = false;
          motionWake = false;
          motionWindowStart = 0;
        }
      } else {
        cryAbove = false;
      }
    }
  }

  //—— manage playback loop ——
  if (mp3->isRunning()) {
    digitalWrite(ALERT_LED_PIN, HIGH);
    mp3->loop();
    // once playback finishes, LED goes off
    if (!mp3->isRunning()) {
      digitalWrite(ALERT_LED_PIN, LOW);
      Serial.println(" Lullaby finished.");
    }
  }

  delay(10);
}

// send a push/HTTP warning to parents’ app
void sendWarningToApp() {
  // TODO: implement your HTTP POST or push-notification here
  Serial.println("[APP] Warning: baby crying!");
}

// send vibrate command to the phone via your app
void sendVibrateCommand() {
  // TODO: implement your HTTP call to trigger phone vibration
  Serial.println("[APP] Command: vibrate phone");
}

// reset all state when turning off
void resetAll() {
  // motion
  motionCount = 0;
  motionWindowStart = 0;
  motionWake = false;
  // cry
  cryAbove = false;
  // lullaby
  lullabyCount = 0;
  // stop any playing song
  if (mp3->isRunning()) mp3->stop();
  digitalWrite(ALERT_LED_PIN, LOW);
}
