#include <ESP8266TimerInterrupt.h>
#include <ESP8266_ISR_Timer.h>
#include <ESP8266_ISR_Timer.hpp>

#define USE_TIMER_1 true
#define TIMER_FREQUENCY_HZ 50
#include <Arduino.h>

#include <SoftwareSerial.h>
#include "SDP600.h"

#define MYPORT_TX 14  // d5
#define MYPORT_RX 12  // d6

EspSoftwareSerial::UART esp_serial;

ESP8266Timer ITimer;

SDP600 sensor;

bool volatile poll_sensor;
void TimerHandler()
{
  poll_sensor = true;
}

void setup() {
  Serial.begin(115200);
  while(!Serial) {
    yield();
  }
  delay(100);

  esp_serial.begin(9600, SWSERIAL_8N1, MYPORT_RX, MYPORT_TX, false);
  sensor.begin();

  // Start timer
  if (!ITimer.attachInterrupt(TIMER_FREQUENCY_HZ, TimerHandler)) {
    Serial.println("Starting Timer failed!");
  }
}

void loop() {
  if (poll_sensor) {
    // load new measurement from sensor
    float measurement = sensor.read();
    poll_sensor = false;

    // send 4 bytes of measurement via serial connection
    esp_serial.write(reinterpret_cast<char*>(&measurement), 4);
    // Serial.write(reinterpret_cast<char*>(&measurement), sizeof(measurement));
    Serial.print(measurement,4);
    Serial.println("");
  }
  
}
