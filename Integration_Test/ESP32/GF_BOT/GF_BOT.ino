#include <ESP32Servo.h>
#include "camera.c"
#include "face_track.c"

void setup() {
  // put your setup code here, to run once:
  Serial.begin(1500000);
  camera_setup();
  servo_setup();
}

void loop() {
  // put your main code here, to run repeatedly:
  
  servo_loop();
  camera_loop(50);
}
