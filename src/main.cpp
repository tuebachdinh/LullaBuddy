// #include <Arduino.h>
// #include <WiFi.h>
// #include <AudioFileSourceHTTPStream.h>
// #include <AudioGeneratorMP3.h>
// #include <AudioOutputI2S.h>

// // -----------------------------
// // WiFi Credentials
// // -----------------------------
// const char* ssid     = "aalto open";
// const char* password = "";

// // URL to your MP3 stream:
// const char* songURL  = "http://ice1.somafm.com/groovesalad-128-mp3";

// // -----------------------------
// // Sound Sensor Configuration
// // -----------------------------
// // MAX4466 analog output on an ADC-capable pin:
// const int   SOUND_SENSOR_PIN      = 34;  
// // Tune for your environment:
// const int   SOUND_THRESHOLD       = 2000;  
// // Required time above threshold (ms) to START playback:
// const unsigned long REQUIRED_TIME_ABOVE   = 2000;  
// // Required time below threshold (ms) to STOP playback:
// const unsigned long REQUIRED_TIME_BELOW   = 8000;  

// // -----------------------------
// // Audio Objects
// // -----------------------------
// AudioFileSourceHTTPStream *file;
// AudioGeneratorMP3         *mp3;
// AudioOutputI2S            *out;

// // -----------------------------
// // Filter & State Tracking
// // -----------------------------
// // float          filteredValue        = 0.0;  // For exponential filtering
// // const float    FILTER_ALPHA         = 0.2;  // 0 < alpha < 1 => alpha=0.2 is fairly moderate filtering

// bool           aboveThreshold       = false;
// bool           triggeredPlayback    = false;

// unsigned long  thresholdAboveStart  = 0;
// unsigned long  thresholdBelowStart  = 0; 

// void setup() {
//   Serial.begin(115200);
//   analogReadResolution(12);
//   analogSetAttenuation(ADC_11db);
//   // Initialize sound sensor pin
//   pinMode(SOUND_SENSOR_PIN, INPUT);

//   // Connect to Wi-Fi
//   WiFi.begin(ssid, password);
//   while (WiFi.status() != WL_CONNECTED) {
//     delay(500);
//     Serial.print(".");
//   }
//   Serial.println("\nConnected to WiFi!");

//   // Prepare I2S audio output
//   out = new AudioOutputI2S();
//   // BCLK=26, LRC=25, DIN=22 (adjust for your wiring)
//   out->SetPinout(26, 25, 22);
//   out->SetOutputModeMono(true); // For MAX98357A
//   out->SetGain(0.2);            // 0.0 - 1.0 volume

//   // Prepare the MP3 file source but don't begin playback yet
//   file = new AudioFileSourceHTTPStream(songURL);
//   mp3  = new AudioGeneratorMP3();

//   // Initialize the filteredValue to avoid a big jump on first read
//   // filteredValue = analogRead(SOUND_SENSOR_PIN);

//   Serial.println("Setup complete. Waiting for continuous sound above threshold...");
// }

// void loop() {
//   // 1) Read the microphone’s raw analog value
//   int filteredValue = analogRead(SOUND_SENSOR_PIN);

//   // 2) Exponential filtering to reduce noise spikes

//   // filteredValue(t) = alpha*rawValue + (1-alpha)*filteredValue(t-1)
//   //filteredValue = FILTER_ALPHA * rawValue + (1.0f - FILTER_ALPHA) * filteredValue;

//   Serial.println(filteredValue);
//   // 3) Check if filtered value is above threshold
//   bool isAbove = (filteredValue > SOUND_THRESHOLD);

//   // ------------------------------------------------------
//   // START Playback Logic - Require 5 seconds above
//   // ------------------------------------------------------
//   if (isAbove) {
//     if (!aboveThreshold) {
//       // Just transitioned from below threshold to above
//       aboveThreshold = true;
//       thresholdAboveStart = millis(); 
//       Serial.println("Above threshold detected; timing started for playback trigger.");
//     } else {
//       // Already above threshold; check elapsed time
//       unsigned long elapsedAbove = millis() - thresholdAboveStart;
//       if (!triggeredPlayback && (elapsedAbove >= REQUIRED_TIME_ABOVE)) {
//         // 5 seconds above threshold => start playback
//         Serial.println("5s above threshold => Starting MP3 playback...");
//         mp3->begin(file, out);
//         triggeredPlayback = true;
//       }
//     }

//     // Since we are above threshold, reset below-threshold tracking
//     thresholdBelowStart = 0;

//   } else {
//     // Below threshold
//     if (aboveThreshold) {
//       // Just transitioned from above threshold to below
//       aboveThreshold = false;
//       thresholdBelowStart = millis();
//       Serial.println("Below threshold detected; timing started for stopping playback.");
//     } else {
//       // Already below threshold; check if playback should stop
//       if (triggeredPlayback && thresholdBelowStart != 0) {
//         unsigned long elapsedBelow = millis() - thresholdBelowStart;
//         if (elapsedBelow >= REQUIRED_TIME_BELOW) {
//           // 5 seconds below threshold => stop playback
//           if (mp3->isRunning()) {
//             mp3->stop();
//             Serial.println("5s below threshold => Stopping MP3 playback.");
//           }
//           triggeredPlayback = false;
//         }
//       }
//     }
//   }

//   // 4) Keep streaming if MP3 is running
//   if (mp3->isRunning()) {
//     mp3->loop();
//   } 

//   delay(100);

// }

#include "esp_camera.h"
#include <WiFi.h>

//
// WARNING!!! PSRAM IC required for UXGA resolution and high JPEG quality
//            Ensure ESP32 Wrover Module or other board with PSRAM is selected
//            Partial images will be transmitted if image exceeds buffer size
//
//            You must select partition scheme from the board menu that has at least 3MB APP space.
//            Face Recognition is DISABLED for ESP32 and ESP32-S2, because it takes up from 15
//            seconds to process single frame. Face Detection is ENABLED if PSRAM is enabled as well

// ===================
// Select camera model
// ===================
#define CAMERA_MODEL_XIAO_ESP32S3 // Has PSRAM
#include "camera_pins.h"

// ===========================
// Enter your WiFi credentials
// ===========================
const char *ssid = "yuuu";
const char *password = "servin022";

void startCameraServer();
void setupLedFlash(int pin);

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.frame_size = FRAMESIZE_UXGA;
  config.pixel_format = PIXFORMAT_JPEG;  // for streaming
  //config.pixel_format = PIXFORMAT_RGB565; // for face detection/recognition
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  // if PSRAM IC present, init with UXGA resolution and higher JPEG quality
  //                      for larger pre-allocated frame buffer.
  if (config.pixel_format == PIXFORMAT_JPEG) {
    if (psramFound()) {
      config.jpeg_quality = 10;
      config.fb_count = 2;
      config.grab_mode = CAMERA_GRAB_LATEST;
    } else {
      // Limit the frame size when PSRAM is not available
      config.frame_size = FRAMESIZE_SVGA;
      config.fb_location = CAMERA_FB_IN_DRAM;
    }
  } else {
    // Best option for face detection/recognition
    config.frame_size = FRAMESIZE_240X240;
#if CONFIG_IDF_TARGET_ESP32S3
    config.fb_count = 2;
#endif
  }

#if defined(CAMERA_MODEL_ESP_EYE)
  pinMode(13, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
#endif

  // camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  sensor_t *s = esp_camera_sensor_get();
  // initial sensors are flipped vertically and colors are a bit saturated
  if (s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1);        // flip it back
    s->set_brightness(s, 1);   // up the brightness just a bit
    s->set_saturation(s, -2);  // lower the saturation
  }
  // drop down frame size for higher initial frame rate
  if (config.pixel_format == PIXFORMAT_JPEG) {
    s->set_framesize(s, FRAMESIZE_QVGA);
  }

#if defined(CAMERA_MODEL_M5STACK_WIDE) || defined(CAMERA_MODEL_M5STACK_ESP32CAM)
  s->set_vflip(s, 1);
  s->set_hmirror(s, 1);
#endif

#if defined(CAMERA_MODEL_ESP32S3_EYE)
  s->set_vflip(s, 1);
#endif

// Setup LED FLash if LED pin is defined in camera_pins.h
#if defined(LED_GPIO_NUM)
  setupLedFlash(LED_GPIO_NUM);
#endif

  WiFi.begin(ssid, password);
  WiFi.setSleep(false);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");

  startCameraServer();

  Serial.print("Camera Ready! Use 'http://");
  Serial.print(WiFi.localIP());
  Serial.println("' to connect");
}

void loop() {
  // Do nothing. Everything is done in another task by the web server
  delay(10000);
}