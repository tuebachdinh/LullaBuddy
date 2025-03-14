#include <Arduino.h>
#include <WiFi.h>
#include <AudioFileSourceHTTPStream.h>
#include <AudioGeneratorMP3.h>
#include <AudioOutputI2S.h>
#include "esp_camera.h"
#define CAMERA_MODEL_XIAO_ESP32S3 // Has PSRAM
#include "pins_layout.h"


// -----------------------------
// WiFi and Streaming Credentials
// -----------------------------
const char* ssid     = "yuuu";
const char* password = "servin022";

// -----------------------------
// MP3 Streaming (song) URL
// -----------------------------
const char* songURL  = "http://ice1.somafm.com/groovesalad-128-mp3";
float gain = 0.2;


const int SOUND_THRESHOLD  = 2000;  // Adjust for your environment

// Timing requirements (in milliseconds)
const unsigned long REQUIRED_TIME_ABOVE = 5000; // 5 seconds above threshold to trigger playback
const unsigned long REQUIRED_TIME_BELOW = 5000; // 5 seconds below threshold to stop playback

// -----------------------------
// Global Variables for Sensor & Playback Control
// -----------------------------
bool aboveThreshold = false;         // True when a trigger condition (sound or motion) is active
bool triggeredPlayback = false;      // True if the song is currently playing
unsigned long thresholdAboveStart = 0; // Timestamp when trigger condition started
unsigned long thresholdBelowStart = 0; // Timestamp when trigger condition stopped

// -----------------------------
// Audio Playback Objects
// -----------------------------
AudioFileSourceHTTPStream *file;
AudioGeneratorMP3         *mp3;
AudioOutputI2S            *out;

// -----------------------------
// Forward Declaration for Camera Server
// (This function is implemented in app_httpd.cpp.)
// -----------------------------
void startCameraServer();
void setupLedFlash(int pin);

// -----------------------------
// Combined Setup Function
// -----------------------------
void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

  // --- Sound & Motion Sensors Setup ---
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  pinMode(SOUND_SENSOR_PIN, INPUT);
  pinMode(MOTION_SENSOR_PIN, INPUT);

  // --- Camera Configuration Setup ---
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0      = Y2_GPIO_NUM;
  config.pin_d1      = Y3_GPIO_NUM;
  config.pin_d2      = Y4_GPIO_NUM;
  config.pin_d3      = Y5_GPIO_NUM;
  config.pin_d4      = Y6_GPIO_NUM;
  config.pin_d5      = Y7_GPIO_NUM;
  config.pin_d6      = Y8_GPIO_NUM;
  config.pin_d7      = Y9_GPIO_NUM;
  config.pin_xclk    = XCLK_GPIO_NUM;
  config.pin_pclk    = PCLK_GPIO_NUM;
  config.pin_vsync   = VSYNC_GPIO_NUM;
  config.pin_href    = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn    = PWDN_GPIO_NUM;
  config.pin_reset   = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.frame_size  = FRAMESIZE_UXGA;
  config.pixel_format = PIXFORMAT_JPEG;  // For streaming
  config.grab_mode   = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count    = 1;

  // Adjust configuration if PSRAM is available
  if (config.pixel_format == PIXFORMAT_JPEG) {
    if (psramFound()) {
      config.jpeg_quality = 10;
      config.fb_count = 2;
      config.grab_mode = CAMERA_GRAB_LATEST;
    } else {
      config.frame_size = FRAMESIZE_SVGA;
      config.fb_location = CAMERA_FB_IN_DRAM;
    }
  } else {
    config.frame_size = FRAMESIZE_240X240;
#if CONFIG_IDF_TARGET_ESP32S3
    config.fb_count = 2;
#endif
  }

  // Initialize camera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }
  sensor_t *s = esp_camera_sensor_get();
  if (s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1);        // Correct vertical flip
    s->set_brightness(s, 1);
    s->set_saturation(s, -2);
  }
  // For better initial frame rate when streaming, set a smaller resolution
  if (config.pixel_format == PIXFORMAT_JPEG) {
    s->set_framesize(s, FRAMESIZE_QVGA);
  }

  // Setup LED flash if available
#if defined(LED_GPIO_NUM)
  setupLedFlash(LED_GPIO_NUM);
#endif

  // --- WiFi Connection ---
  WiFi.begin(ssid, password);
  WiFi.setSleep(false);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");

  // --- Start the Camera Server ---
  startCameraServer();
  Serial.print("Camera Ready! Use 'http://");
  Serial.print(WiFi.localIP());
  Serial.println("' to connect");

  // --- Audio Playback Setup ---
  out = new AudioOutputI2S();
  out->SetPinout(BCLK_AMPLIFIER_PIN, LRC_AMPLIFIER_PIN, DIN_AMPLIFIER_PIN);
  out->SetOutputModeMono(true); // For example, if using a MAX98357A board
  out->SetGain(gain);            // Volume setting: 0.0 - 1.0

  file = new AudioFileSourceHTTPStream(songURL);
  mp3  = new AudioGeneratorMP3();

  Serial.println("Setup complete. Monitoring sound and motion sensors...");
}

// -----------------------------
// Combined Loop: Monitor Sound and Motion Sensors
// -----------------------------
void loop() {
  // Read sound sensor value (analog)
  int soundValue = analogRead(SOUND_SENSOR_PIN);
  bool soundDetected = (soundValue > SOUND_THRESHOLD);

  // Read motion sensor value (digital)
  bool motionDetected = (digitalRead(MOTION_SENSOR_PIN) == HIGH); // Assumes HIGH means motion detected

  // Combine both triggers; if either is active then we consider a trigger condition
  bool triggerDetected = soundDetected || motionDetected;

  // Print sensor readings (for debugging)
  Serial.print("Sound: ");
  Serial.print(soundValue);
  Serial.print(" | Motion: ");
  Serial.println(motionDetected ? "YES" : "NO");

  if (triggerDetected) {
    if (!aboveThreshold) {
      aboveThreshold = true;
      thresholdAboveStart = millis();
      if (soundDetected && motionDetected) {
        Serial.println("Both motion and crying detected! [HIGH ALERT]");
      } else if (motionDetected) {
        Serial.println("Motion detected! [Warning]");
      } else if (soundDetected) {
        Serial.println("Sound (crying) detected! [Warning]");
      }
    } else {
      unsigned long elapsedAbove = millis() - thresholdAboveStart;
      if (!triggeredPlayback && (elapsedAbove >= REQUIRED_TIME_ABOVE)) {
        Serial.println("Trigger sustained for 5 seconds => Starting MP3 playback...");
        mp3->begin(file, out);
        triggeredPlayback = true;
      }
    }
    // Reset below-threshold timer since a trigger is active
    thresholdBelowStart = 0;
  } else {
    if (aboveThreshold) {
      aboveThreshold = false;
      thresholdBelowStart = millis();
      Serial.println("No sound/motion detected; starting timer to stop playback.");
    } else {
      if (triggeredPlayback && thresholdBelowStart != 0) {
        unsigned long elapsedBelow = millis() - thresholdBelowStart;
        if (elapsedBelow >= REQUIRED_TIME_BELOW) {
          if (mp3->isRunning()) {
            mp3->stop();
            Serial.println("No trigger for required time => Stopping MP3 playback.");
          }
          triggeredPlayback = false;
        }
      }
    }
  }

  // Continue processing MP3 playback if running
  if (mp3->isRunning()) {
    mp3->loop();
  }

  delay(100);
}
