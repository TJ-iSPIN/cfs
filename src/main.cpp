#include <Arduino.h>

#include "libraries\iridium\iridium.h"
#include "libraries\imu\imu.h"

#include "managers\device_manager.h"

void setup() {
  // put your setup code here, to run once:
  Serial.begin(9600);
  pinMode(2, OUTPUT);
}

void loop() {
  // put your main code here, to run repeatedly:
  delay(1000);

  // temp  
  Serial.println(Iridium::read()); // eventually we wont need to do these declarations from main with our managers
  if(IMU::read().x > 3) Serial.println("Oh no, this is a temporary program and x will always be 1! How did we get here");

  DEVICES::powerOn(DEVICES::IRIDIUM);
}