#include <ESP8266TimerInterrupt.h>
#include <ESP8266_ISR_Timer.h>
#include <ESP8266_ISR_Timer.hpp>

#define USE_TIMER_1 true
#define TIMER_FREQUENCY_HZ 50
#include <Arduino.h>

#include "SDP600.h"
#include <SoftwareSerial.h>

#define MYPORT_TX 14 // d5
#define MYPORT_RX 12 // d6

EspSoftwareSerial::UART esp_serial;

ESP8266Timer ITimer;

SDP600 sensor;
uint32_t measurement_counter;

bool volatile poll_sensor;
void TimerHandler() { poll_sensor = true; }

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    yield();
  }
  delay(100);

  esp_serial.begin(9600, SWSERIAL_8N1, MYPORT_RX, MYPORT_TX, false);
  sensor.begin();

  measurement_counter = 0;

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

    // create data package to send to web server esp
    char data_package[10];
    data_package[0] = '\xFF'; // package starts with a full one byte
    data_package[9] = '\x00'; // package ends with a zero byte
    float *measurement_ptr = reinterpret_cast<float *>(&(data_package[1]));
    uint32_t *index_ptr = reinterpret_cast<uint32_t *>(&(data_package[5]));
    *measurement_ptr = measurement;
    *index_ptr = measurement_counter;

    // send 10 bytes of data package via serial connection
    esp_serial.write(reinterpret_cast<char *>(&data_package), 10);
    // Serial.write(reinterpret_cast<char*>(&measurement), sizeof(measurement));
    Serial.print(measurement_counter);
    Serial.print(" ");
    Serial.print(measurement);
    Serial.println("");
    // increase measurement counter
    measurement_counter += 1;
  }
}
