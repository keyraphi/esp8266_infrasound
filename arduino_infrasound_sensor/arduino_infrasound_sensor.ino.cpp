#define USE_TIMER_1 true
#define TIMER_FREQUENCY_HZ 50

#include <Arduino.h>
#include <TimerInterrupt.h>
#include <TimerInterrupt.hpp>
#include <ISR_Timer.h>
#include <ISR_Timer.hpp>

#include <SoftwareSerial.h>

#include "SDP600.h"

#define MYPORT_TX 3
#define MYPORT_RX 2

//SoftwareSerial esp_serial(MYPORT_TX, MYPORT_RX);

SDP600 sensor;

bool volatile poll_sensor;
void TimerHandler()
{
  poll_sensor = true;
}

void setup() {
  Serial.begin(38400);
  while(!Serial) {
    yield();
  }
  delay(100);

//  esp_serial.begin(38400, SWSERIAL_8N1);

  sensor.begin();

  // Start timer
  ITimer1.init();
  if (!ITimer1.attachInterrupt(TIMER_FREQUENCY_HZ * 2, TimerHandler)) {
    Serial.println("Starting Timer failed!");
  }
  pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
  if (poll_sensor) {
    digitalWrite(LED_BUILTIN, HIGH);
    // load new measurement from sensor
    float measurement = sensor.read();
    poll_sensor = false;

    // send 4 bytes of measurement via serial connection
    //esp_serial.write(reinterpret_cast<char*>(&measurement), 4);
    Serial.write(reinterpret_cast<char*>(&measurement), sizeof(measurement));

    digitalWrite(LED_BUILTIN, LOW);
  }
  
}
