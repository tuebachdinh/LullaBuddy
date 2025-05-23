#include <WiFi.h>
#include "esp_camera.h"
#include "esp_http_server.h"

#define CAMERA_MODEL_XIAO_ESP32S3
#include "pins_layout.h"  // BCLK_, LRC_, DIN_, MOTION_SENSOR_PIN, SOUND_SENSOR_PIN
#include "camera_index.h"
// Select your camera model and include the proper pin definitions

 // ensure this defines Y2_GPIO_NUM, etc.

// Your network credentials
const char* ssid     = "yuuu";
const char* password = "servin022";

// HTTP server handle
httpd_handle_t camera_httpd = NULL;

// Handler to serve a simple HTML page showing the image
static esp_err_t index_handler(httpd_req_t *req) {
    const char* html =
        "<html><head><meta http-equiv=\"refresh\" content=\"2;url=/\" />"
        "<title>ESP32-CAM Live</title></head><body>"
        "<h1>ESP32-CAM Snapshot</h1>"
        "<img src=\"/capture\" style=\"width:100%;max-width:640px;\" />"
        "</body></html>";
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html, strlen(html));
    return ESP_OK;
}

// Handler to serve a single JPEG frame at /capture
static esp_err_t capture_handler(httpd_req_t *req) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
    httpd_resp_send(req, (const char *)fb->buf, fb->len);
    esp_camera_fb_return(fb);
    return ESP_OK;
}

// Configure and start the HTTP server
void startCameraServer() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 2;

    if (httpd_start(&camera_httpd, &config) == ESP_OK) {
        httpd_uri_t index_uri = {
            .uri       = "/",
            .method    = HTTP_GET,
            .handler   = index_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(camera_httpd, &index_uri);

        httpd_uri_t cap_uri = {
            .uri       = "/capture",
            .method    = HTTP_GET,
            .handler   = capture_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(camera_httpd, &cap_uri);
    }
}

void setup() {
    Serial.begin(115200);

    // 1) Initialize camera
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
    if (esp_camera_init(&config) != ESP_OK) {
        Serial.println("❌ Camera init failed");
        while (true) { delay(1000); }
    }

    // 2) Connect to Wi-Fi
    Serial.printf("Connecting to Wi-Fi '%s'...", ssid);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.printf("\n✅ Wi-Fi connected, IP: %s\n", WiFi.localIP().toString().c_str());

    // 3) Start HTTP server
    startCameraServer();
    Serial.printf("→ Open http://%s/ in your browser to view snapshots\n", WiFi.localIP().toString().c_str());
}

void loop() {
    // nothing to do here; the HTTP server handles requests in the background
    delay(1000);
}
