#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <AudioFileSourceHTTPStream.h>
#include <AudioFileSourceBuffer.h>
#include <AudioGeneratorMP3.h>
#include <AudioOutputI2S.h>
#include "esp_camera.h"
#define CAMERA_MODEL_XIAO_ESP32S3  // Has PSRAM
#include "pins_layout.h"  // defines BCLK_AMPLIFIER_PIN, LRC_AMPLIFIER_PIN, DIN_AMPLIFIER_PIN

//=== Configuration ===
const char* ssid      = "yuuu";
const char* password  = "servin022";
const char* songURL   = "http://ia600107.us.archive.org/13/items/LullabySong/06-nickelback-lullaby.mp3";
const float  gain     = 0.1f;
const size_t BUF_SIZE = 256 * 1024;  // 8 KB buffer

//=== Audio components ===
AudioFileSourceHTTPStream *file   = nullptr;
AudioFileSourceBuffer     *buffer = nullptr;
AudioGeneratorMP3         *mp3    = nullptr;
AudioOutputI2S            *out    = nullptr;

void setup() {
  Serial.begin(115200);

  // — Wi-Fi setup —
  WiFi.begin(ssid, password);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print('.');
  }
  Serial.println();
  Serial.print("WiFi connected, IP: ");
  Serial.println(WiFi.localIP());

  // — I2S audio output (mono) —
  out = new AudioOutputI2S();
  out->SetPinout(BCLK_AMPLIFIER_PIN, LRC_AMPLIFIER_PIN, DIN_AMPLIFIER_PIN);
  out->SetOutputModeMono(true);
  out->SetGain(gain);

  // — Buffered HTTP MP3 source & decoder —
  file   = new AudioFileSourceHTTPStream(songURL);
  buffer = new AudioFileSourceBuffer(file, BUF_SIZE);
  mp3    = new AudioGeneratorMP3();

  Serial.println("Starting buffered MP3 stream...");
  mp3->begin(buffer, out);
}

void loop() {
  if (mp3->isRunning()) {
    // Feed the decoder; background task fills the buffer from Wi-Fi
    mp3->loop();
  } else {
    // End of stream or error – tear down and restart
    Serial.println("Stream ended. Restarting in 1 s...");
    // 1) Stop & delete decoder
    mp3->stop();
    delete mp3;
    mp3 = nullptr;

    // 2) Close & delete buffer
    buffer->close();
    delete buffer;
    buffer = nullptr;

    // 3) Close & delete HTTP source
    file->close();
    delete file;
    file = nullptr;

    delay(1000);

    // 4) Re-create source, buffer, decoder
    file   = new AudioFileSourceHTTPStream(songURL);
    buffer = new AudioFileSourceBuffer(file, BUF_SIZE);
    mp3    = new AudioGeneratorMP3();

    // 5) Restart playback
    mp3->begin(buffer, out);
  }
}