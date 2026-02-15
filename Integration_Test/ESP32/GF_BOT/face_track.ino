#include <ESP32Servo.h>

Servo servo;
const int SERVO_PIN = 2;  // signal pin
int angle = 90;

void servo_setup() {
  // ESP32 PWM settings for servo
  servo.setPeriodHertz(50);            // 50Hz for standard servos
  servo.attach(SERVO_PIN, 500, 2400);  // min/max pulse in microseconds

  servo.write(angle);
  Serial.println("Ready. Send angle 0-180 in Serial Monitor.");
}

void servo_loop() {
  if (Serial.available()) {
    int a = Serial.parseInt();
    if (a >= 0 && a <= 180) {
      angle = a;
      servo.write(angle);
      Serial.print("Angle = ");
      Serial.println(angle);
    } else {
      Serial.println("Send a number 0-180");
    }
  }
}