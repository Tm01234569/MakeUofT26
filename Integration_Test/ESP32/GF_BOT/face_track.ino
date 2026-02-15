#include <ESP32Servo.h>

Servo servo;
const int SERVO_PIN = 2;
int angle = 90;

String cmdLine;

void servo_setup() {
  servo.setPeriodHertz(50);
  servo.attach(SERVO_PIN, 500, 2400);
  servo.write(angle);

  // IMPORTANT: prevents readStringUntil/serial reads from blocking long
  Serial.setTimeout(2);
}

void servo_loop() {
  while (Serial.available()) {
    char c = (char)Serial.read();

    if (c == '\n') {
      cmdLine.trim();

      // Only accept commands like: ANG:123
      if (cmdLine.startsWith("ANG:")) {
        int a = cmdLine.substring(4).toInt();
        if (a < 0) a = 0;
        if (a > 180) a = 180;
        angle = a;
        servo.write(angle);
      }

      cmdLine = "";  // clear for next command
    }
    else if (c != '\r') {
      // Keep buffer small so random bytes don't explode it
      if (cmdLine.length() < 32) cmdLine += c;
      else cmdLine = "";
    }
  }
}
