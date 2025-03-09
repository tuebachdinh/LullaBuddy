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

// URL to your MP3 stream:
const char* songURL  = "http://ice1.somafm.com/groovesalad-128-mp3";

// -----------------------------
// Sound Sensor Configuration
// -----------------------------
// MAX4466 analog output on an ADC-capable pin:
const int   SOUND_SENSOR_PIN      = 34;  
// Tune for your environment:
const int   SOUND_THRESHOLD       = 2000;  
// Required time above threshold (ms) to START playback:
const unsigned long REQUIRED_TIME_ABOVE   = 2000;  
// Required time below threshold (ms) to STOP playback:
const unsigned long REQUIRED_TIME_BELOW   = 8000;  

// -----------------------------
// Audio Objects
// -----------------------------
AudioFileSourceHTTPStream *file;
AudioGeneratorMP3         *mp3;
AudioOutputI2S            *out;

// -----------------------------
// Filter & State Tracking
// -----------------------------
// float          filteredValue        = 0.0;  // For exponential filtering
// const float    FILTER_ALPHA         = 0.2;  // 0 < alpha < 1 => alpha=0.2 is fairly moderate filtering

bool           aboveThreshold       = false;
bool           triggeredPlayback    = false;

unsigned long  thresholdAboveStart  = 0;
unsigned long  thresholdBelowStart  = 0; 

void setup() {
  Serial.begin(115200);
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
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
  // BCLK=26, LRC=25, DIN=22 (adjust for your wiring)
  out->SetPinout(26, 25, 22);
  out->SetOutputModeMono(true); // For MAX98357A
  out->SetGain(0.2);            // 0.0 - 1.0 volume

  // Prepare the MP3 file source but don't begin playback yet
  file = new AudioFileSourceHTTPStream(songURL);
  mp3  = new AudioGeneratorMP3();

  // Initialize the filteredValue to avoid a big jump on first read
  // filteredValue = analogRead(SOUND_SENSOR_PIN);

  Serial.println("Setup complete. Waiting for continuous sound above threshold...");
}

void loop() {
  // 1) Read the microphone’s raw analog value
  int filteredValue = analogRead(SOUND_SENSOR_PIN);

  // 2) Exponential filtering to reduce noise spikes

  // filteredValue(t) = alpha*rawValue + (1-alpha)*filteredValue(t-1)
  //filteredValue = FILTER_ALPHA * rawValue + (1.0f - FILTER_ALPHA) * filteredValue;

  Serial.println(filteredValue);
  // 3) Check if filtered value is above threshold
  bool isAbove = (filteredValue > SOUND_THRESHOLD);

  // ------------------------------------------------------
  // START Playback Logic - Require 5 seconds above
  // ------------------------------------------------------
  if (isAbove) {
    if (!aboveThreshold) {
      // Just transitioned from below threshold to above
      aboveThreshold = true;
      thresholdAboveStart = millis(); 
      Serial.println("Above threshold detected; timing started for playback trigger.");
    } else {
      // Already above threshold; check elapsed time
      unsigned long elapsedAbove = millis() - thresholdAboveStart;
      if (!triggeredPlayback && (elapsedAbove >= REQUIRED_TIME_ABOVE)) {
        // 5 seconds above threshold => start playback
        Serial.println("5s above threshold => Starting MP3 playback...");
        mp3->begin(file, out);
        triggeredPlayback = true;
      }
    }

    // Since we are above threshold, reset below-threshold tracking
    thresholdBelowStart = 0;

  } else {
    // Below threshold
    if (aboveThreshold) {
      // Just transitioned from above threshold to below
      aboveThreshold = false;
      thresholdBelowStart = millis();
      Serial.println("Below threshold detected; timing started for stopping playback.");
    } else {
      // Already below threshold; check if playback should stop
      if (triggeredPlayback && thresholdBelowStart != 0) {
        unsigned long elapsedBelow = millis() - thresholdBelowStart;
        if (elapsedBelow >= REQUIRED_TIME_BELOW) {
          // 5 seconds below threshold => stop playback
          if (mp3->isRunning()) {
            mp3->stop();
            Serial.println("5s below threshold => Stopping MP3 playback.");
          }
          triggeredPlayback = false;
        }
      }
    }
  }

  // 4) Keep streaming if MP3 is running
  if (mp3->isRunning()) {
    mp3->loop();
  } 

  delay(100);

}

// Define the pin connections
// Arduino code to test the AMN-Series PIR sensor

// #define PIR_PIN 2      // Digital pin connected to sensor's OUT pin

// void setup() {
//   Serial.begin(115200);         // Start serial communication
//   pinMode(PIR_PIN, INPUT);      // Set the PIR sensor pin as input
  
//   // Inform the user and allow sensor calibration time
//   Serial.println("Initializing AMN-Series PIR sensor...");
//   Serial.println("Please wait 30 seconds for calibration...");
//   delay(30000);               // Typical warm-up/calibration period
//   Serial.println("Sensor is now ready!");
// }

// void loop() {
//   int sensorState = digitalRead(PIR_PIN); // Read sensor output

//   if (sensorState == HIGH) {
//     Serial.println("Motion detected!");
    
//   } else {
//     Serial.println("No motion.");

//   }
  
//   delay(500); // Short delay before next reading
// }
