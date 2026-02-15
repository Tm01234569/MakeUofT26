#include "esp_camera.h"
#include <ESP32Servo.h>

// ========= Camera pins (your external camera wiring) =========
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     15
#define SIOD_GPIO_NUM     4
#define SIOC_GPIO_NUM     5

#define Y9_GPIO_NUM       16
#define Y8_GPIO_NUM       17
#define Y7_GPIO_NUM       18
#define Y6_GPIO_NUM       12
#define Y5_GPIO_NUM       10
#define Y4_GPIO_NUM       8
#define Y3_GPIO_NUM       9
#define Y2_GPIO_NUM       11
#define VSYNC_GPIO_NUM    6
#define HREF_GPIO_NUM     7
#define PCLK_GPIO_NUM     13

// ========= Servo =========
static const int SERVO_PIN = 2;     // <-- use GPIO2 (not used by camera)
Servo servo;
int servoAngle = 90;

// Serial baud for streaming
static const int BAUD = 1500000;

// For QQVGA grayscale: 160x120 = 19200 bytes
static const int FRAME_BYTES = 160 * 120;

void setup() {
  Serial.begin(BAUD);
  delay(300);

  // Servo setup
  servo.setPeriodHertz(50);
  servo.attach(SERVO_PIN, 500, 2400);
  servo.write(servoAngle);

  Serial.println("Initializing camera...");

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;

  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;

  config.pin_xclk  = XCLK_GPIO_NUM;
  config.pin_pclk  = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href  = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn  = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;

  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_GRAYSCALE;

  config.frame_size   = FRAMESIZE_QQVGA;  // 160x120
  config.jpeg_quality = 12;
  config.fb_count     = 2;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    while (true) delay(1000);
  }

  Serial.println("Starting!");
}

void handleAngleCommandNonBlocking() {
  // Read a line if available (ASCII command)
  if (!Serial.available()) return;

  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.startsWith("ANG:")) {
    int a = line.substring(4).toInt();
    if (a < 0) a = 0;
    if (a > 180) a = 180;
    servoAngle = a;
    servo.write(servoAngle);
    // Optional ack:
    // Serial.printf("OK:%d\n", servoAngle);
  }
}

void loop() {
  // 1) Check for incoming servo commands (quick, non-blocking)
  handleAngleCommandNonBlocking();

  // 2) Capture frame
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("failed");
    delay(10);
    return;
  }

  // Safety: expect grayscale QQVGA length
  if (fb->len != FRAME_BYTES) {
    // Still stream it, but your Python expects 19200 bytes.
    // If this happens often, your frame size/pixel format is mismatched.
  }

  // 3) Stream frame
  Serial.println("START_IMAGE");
  Serial.write(fb->buf, fb->len);
  Serial.println("END_IMAGE");

  esp_camera_fb_return(fb);

  delay(50); // ~20 fps
}
