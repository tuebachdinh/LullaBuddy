#include <Arduino.h>
#include <WiFi.h>
#include <AudioFileSourceHTTPStream.h>
#include <AudioGeneratorMP3.h>
#include <AudioOutputI2S.h>

// -----------------------------
// WiFi Credentials
// -----------------------------
const char* ssid     = "aalto open";
const char* password = "";

// URL to your MP3 stream (e.g., SomaFM, Icecast, etc.)
const char* songURL  = "http://ice1.somafm.com/groovesalad-128-mp3";

// -----------------------------
// Sound Sensor Configuration
// -----------------------------
// MAX4466 analog output is read via an ADC-capable pin:
const int SOUND_SENSOR_PIN  = 34; 
// Calibrate this threshold for your environment:
const int SOUND_THRESHOLD   = 2000;  
// Required time (in milliseconds) above threshold:
const unsigned long REQUIRED_TIME = 3000; // 5 seconds

// -----------------------------
// Audio Objects
// -----------------------------
AudioFileSourceHTTPStream *file;
AudioGeneratorMP3 *mp3;
AudioOutputI2S *out;

// -----------------------------
// State Tracking
// -----------------------------
bool  aboveThreshold       = false;  
bool  triggeredPlayback    = false;  
unsigned long thresholdStartTime = 0;  

void setup() {
  Serial.begin(115200);

  // Initialize sound sensor pin
  pinMode(SOUND_SENSOR_PIN, INPUT);

  // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected to WiFi!");

  // Prepare I2S audio output
  out = new AudioOutputI2S();
  // BCLK=26, LRC=25, DIN=22 (adjust for your wiring):
  out->SetPinout(26, 25, 22);
  out->SetOutputModeMono(true);  // for MAX98357A
  out->SetGain(0.5);             // 0.0 - 1.0 volume

  // Prepare the source and MP3 generator but don't begin playback yet
  file = new AudioFileSourceHTTPStream(songURL);
  mp3  = new AudioGeneratorMP3();

  Serial.println("Setup complete. Waiting for sound above threshold...");
}

void loop() {
  // Read the microphone's analog value:
  int sensorValue = analogRead(SOUND_SENSOR_PIN);

  // Check if the value is above threshold
  if (sensorValue > SOUND_THRESHOLD) {
    Serial.println("Sound above threshold detected.");
    if (!aboveThreshold) {
      // Just transitioned from below threshold to above
      aboveThreshold = true;
      thresholdStartTime = millis(); 
      Serial.println("Sound above threshold detected; timing started.");
    } 
    
    else {
      // We have been above threshold already; check how long
      unsigned long elapsed = millis() - thresholdStartTime;
      if (!triggeredPlayback && (elapsed >= REQUIRED_TIME)) {
        // 5 seconds of continuous sound above threshold
        Serial.println("5 seconds above threshold reached. Starting MP3...");
        mp3->begin(file, out); // Start streaming
        triggeredPlayback = true;
      }
    }
  } 
  
  
  else {
    // Below threshold => reset the above-threshold timer
    if (aboveThreshold) {
      Serial.println("Sound fell below threshold, resetting timer...");
    }
    aboveThreshold = false;
    thresholdStartTime = 0;
  }

  // Keep streaming if MP3 is running
  if (mp3->isRunning()) {
    mp3->loop();
  } else {
    // Avoid tight-loop spamming if not running
    delay(50);
  }
}
