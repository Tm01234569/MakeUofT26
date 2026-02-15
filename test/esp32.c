#include "esp_camera.h"
#include <ESP32Servo.h>

// ================= Camera pins (YOUR external camera wiring) =================
// Keep exactly as you had it.
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

// ================= Settings =================
static const int BAUD = 1500000;
static const int W = 160;
static const int H = 120;

// Servo signal pin (use the one that actually works on your board)
static const int SERVO_PIN = 2;   // <- change to 14 only if your wiring/pin works

// ================= Servo =================
Servo servo;
int servoAngle = 90;
String cmdLine;

// ================= Camera setup =================
void camera_setup() {
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

  config.frame_size   = FRAMESIZE_QQVGA; // 160x120
  config.jpeg_quality = 12;
  config.fb_count     = 2;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    while (true) delay(1000);
  }

  Serial.println("Camera ready!");
}

// ================= Servo setup =================
void servo_setup() {
  servo.setPeriodHertz(50);
  servo.attach(SERVO_PIN, 500, 2500);
  servo.write(servoAngle);
  Serial.println("Servo ready!");
}

// ================= Parse ANG:### commands =================
void servo_loop() {
  while (Serial.available()) {
    char c = (char)Serial.read();

    if (c == '\n') {
      cmdLine.trim();

      if (cmdLine.startsWith("ANG:")) {
        int a = cmdLine.substring(4).toInt();
        if (a < 0) a = 0;
        if (a > 180) a = 180;
        servoAngle = a;
        servo.write(servoAngle);

        // Optional debug (comment out if you want max speed)
        // Serial.print("OK ");
        // Serial.println(servoAngle);
      }

      cmdLine = "";
    }
    else if (c != '\r') {
      // Keep the buffer short so random bytes never break it
      if (cmdLine.length() < 32) cmdLine += c;
      else cmdLine = "";
    }
  }
}

// ================= Stream camera frames =================
void camera_loop(int delay_ms) {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    delay(10);
    return;
  }

  // Markers + raw bytes
  Serial.println("START_IMAGE");
  Serial.write(fb->buf, fb->len);
  Serial.println("END_IMAGE");

  esp_camera_fb_return(fb);
  delay(delay_ms);
}

void setup() {
  Serial.begin(BAUD);
  delay(200);

  servo_setup();
  camera_setup();

  Serial.println("Starting stream...");
}

void loop() {
  // Keep servo responsive
  servo_loop();

  // Stream frames (~20 fps)
  camera_loop(50);
}
