#include <Arduino.h>
#include "test/test.h"

void setup() {
  // put your setup code here, to run once:
  Serial.begin(9600);
  pinMode(2, OUTPUT);
}

void loop() {
  // put your main code here, to run repeatedly:
  doPrint();
  delay(1000);
}