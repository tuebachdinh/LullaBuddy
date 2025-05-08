#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <AudioFileSourceHTTPStream.h>
#include <AudioGeneratorMP3.h>
#include <AudioOutputI2S.h>
#include "esp_camera.h"
#define CAMERA_MODEL_XIAO_ESP32S3  // Has PSRAM
#include "pins_layout.h"            // your BCLK_, LRC_, DIN_ defines

// //=== User-configurable ===
// const char* ssid     = "aalto open";
// const char* password = "";
// // your static MP3 file (no redirect)
// const char* songURL  = "http://ia600107.us.archive.org/13/items/LullabySong/06-nickelback-lullaby.mp3";
// const float  gain    = 0.2;

// //=== Sensor thresholds & timings ===
// const int SOUND_THRESHOLD       = 2000;      // raw mic threshold
// const unsigned long PIR_HIGH_MS = 3000;      // PIR must stay HIGH for 3 s
// const unsigned long CRY_WINDOW_MS = 5000;    // listen 5 s after PIR
// const int  DIFF_THRESHOLD       = 300;       // diff >300 counts as cry spike
// const int  CRY_COUNT_THRESHOLD  = 50;        // spikes per window
// const int  MAX_LULLABIES        = 3;

// //=== Pin assignments ===
// const int PIR_PIN = MOTION_SENSOR_PIN;
// const int MIC_PIN = SOUND_SENSOR_PIN;

// //=== Networking & Test Mode ===
// WebServer server(80);
// bool       testMode = false;

// //=== Audio playback globals ===
// AudioFileSourceHTTPStream *file;
// AudioGeneratorMP3         *mp3;
// AudioOutputI2S            *out;

// //=== Motion / cry state ===
// unsigned long pirHighStart   = 0;
// bool         pirTriggered    = false;
// unsigned long cryWindowStart = 0;
// int          prevSoundV      = 0;
// int          crySpikeCount   = 0;
// int          lullabyCount    = 0;

// //––– Forward declarations
// void startLullaby();
// void sendWarningToApp();
// void sendVibrateCommand();
// void sendTestFeedback(const char* msg);

// void setup() {
//   Serial.begin(115200);

//   // Sensors
//   analogReadResolution(12);
//   analogSetAttenuation(ADC_11db);
//   pinMode(MIC_PIN, INPUT);
//   pinMode(PIR_PIN, INPUT);

//   // Wi-Fi
//   WiFi.begin(ssid, password);
//   WiFi.setSleep(false);
//   while (WiFi.status() != WL_CONNECTED) {
//     delay(500);
//     Serial.print(".");
//   }
//   Serial.println("\nWiFi connected");

//   // HTTP server routes for Test Mode
//   server.on("/test/on", []() {
//     testMode = true;
//     server.send(200, "text/plain", "Test mode ON");
//     Serial.println("==> Test mode ENABLED");
//   });
//   server.on("/test/off", []() {
//     testMode = false;
//     server.send(200, "text/plain", "Test mode OFF");
//     Serial.println("==> Test mode DISABLED");
//   });
//   server.begin();
//   Serial.println("HTTP server started");

//   // Audio output setup
//   out = new AudioOutputI2S();
//   out->SetPinout(BCLK_AMPLIFIER_PIN, LRC_AMPLIFIER_PIN, DIN_AMPLIFIER_PIN);
//   out->SetOutputModeMono(true);
//   out->SetGain(gain);

//   // Prepare MP3 decoder + (empty) HTTP‐stream source
//   file = new AudioFileSourceHTTPStream();
//   mp3  = new AudioGeneratorMP3();

//   Serial.println("Setup complete. Monitoring sensors...");
// }

// // Helper to (re)start the lullaby
// void startLullaby() {
//   // Close any prior connection
//   file->close();
//   delay(50);
//   // Open fresh and hand it to the decoder
//   file->open(songURL);
//   mp3->begin(file, out);
// }

// void loop() {
//   unsigned long now = millis();

//   // —— Test Mode: just report triggers, no lullaby
//   server.handleClient();
//   if (testMode) {
//     if (digitalRead(PIR_PIN))    sendTestFeedback("motion detected");
//     if (analogRead(MIC_PIN) > SOUND_THRESHOLD) sendTestFeedback("sound detected");
//     delay(100);
//     return;
//   }

//   // —— 1) PIR: sustained HIGH → arm cry window
//   bool pir = digitalRead(PIR_PIN);
//   if (!pirTriggered && !mp3->isRunning()) {
//     if (pir) {
//       if (pirHighStart == 0) pirHighStart = now;
//       else if (now - pirHighStart >= PIR_HIGH_MS) {
//         pirTriggered    = true;
//         cryWindowStart  = now;
//         prevSoundV      = analogRead(MIC_PIN);
//         crySpikeCount   = 0;
//         Serial.println(">> PIR sustained HIGH → waking");
//       }
//     } else {
//       pirHighStart = 0;
//     }
//   }

//   // —— 2) Cry detection window
//   if (pirTriggered && !mp3->isRunning()) {
//     if (now - cryWindowStart <= CRY_WINDOW_MS) {
//       int soundV = analogRead(MIC_PIN);
//       int diff   = abs(soundV - prevSoundV);
//       if (diff > DIFF_THRESHOLD) crySpikeCount++;
//       prevSoundV = soundV;

//       if (crySpikeCount >= CRY_COUNT_THRESHOLD) {
//         Serial.println(">> CRY detected!");
//         sendWarningToApp();

//         if (lullabyCount < MAX_LULLABIES) {
//           lullabyCount++;
//           Serial.printf(" Playing lullaby #%d\n", lullabyCount);
//           startLullaby();
//         } else {
//           Serial.println(" Max lullabies reached → vibrate phone");
//           sendVibrateCommand();
//         }

//         // reset for next cycle
//         pirTriggered   = false;
//         pirHighStart   = 0;
//       }
//     } else {
//       Serial.printf(">> Cry window expired (%d spikes), resetting\n", crySpikeCount);
//       pirTriggered = false;
//       pirHighStart = 0;
//     }
//   }

//   // —— 3) Audio playback pump
//   if (mp3->isRunning()) {
//     mp3->loop();
//   }

//   delay(10);
// }

// // stub: send a warning to your parent app
// void sendWarningToApp() {
//   Serial.println("[APP] Warning: baby crying!");
//   // TODO: HTTP POST or push notification
// }

// // stub: tell the phone to vibrate
// void sendVibrateCommand() {
//   Serial.println("[APP] Command: vibrate phone");
//   // TODO: implement your HTTP call here
// }

// // stub: feedback for Test Mode
// void sendTestFeedback(const char* msg) {
//   Serial.printf("[TEST] %s\n", msg);
//   // TODO: send via WebSocket/HTTP to your app
// }



#include <Arduino.h>
#include <WiFi.h>
#include <AudioFileSourceHTTPStream.h>
#include <AudioGeneratorMP3.h>
#include <AudioOutputI2S.h>
#include "pins_layout.h"  // your BCLK_, LRC_, DIN_ defines

//=== Configuration ===
const char* ssid     = "aalto open";
const char* password = "";
// ← Updated to your static MP3
const char* songURL  = "http://ia600107.us.archive.org/13/items/LullabySong/06-nickelback-lullaby.mp3";
const float  gain    = 0.2;

//=== Audio components ===
AudioFileSourceHTTPStream *file;
AudioGeneratorMP3         *mp3;
AudioOutputI2S            *out;

void setup() {
  Serial.begin(115200);

  // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  WiFi.setSleep(false);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");

  // Initialize I2S audio output (mono)
  out = new AudioOutputI2S();
  out->SetPinout(BCLK_AMPLIFIER_PIN, LRC_AMPLIFIER_PIN, DIN_AMPLIFIER_PIN);
  out->SetOutputModeMono(true);
  out->SetGain(gain);

  // Prepare MP3 stream (now a finite file)
  file = new AudioFileSourceHTTPStream(songURL);
  mp3  = new AudioGeneratorMP3();

  // Start playback
  Serial.println("Starting audio stream...");
  mp3->begin(file, out);
}

void loop() {
  if (mp3->isRunning()) {
    mp3->loop();
  } else {
    // file has reached EOF – restart from the top
    Serial.println("Stream ended. Restarting...");
    delay(1000);
    mp3->begin(file, out);
  }
  
}
