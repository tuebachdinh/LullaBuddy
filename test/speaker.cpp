#include <Arduino.h>
#include <WiFi.h>
#include <AudioFileSourceHTTPStream.h>
#include <AudioGeneratorMP3.h>
#include <AudioOutputI2S.h>
#include "pins_layout.h"  // your BCLK_, LRC_, DIN_ defines

//=== Configuration ===
const char* ssid     = "yuuu";
const char* password = "servin022";
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
