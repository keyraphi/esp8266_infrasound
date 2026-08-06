#include <ESP8266TimerInterrupt.h>
#include <ESP8266_ISR_Timer.h>
#include <ESP8266_ISR_Timer.hpp>

#define USE_TIMER_1 true
#define TIMER_FREQUENCY_HZ 50
#include <Arduino.h>

#include "SDP600.h"
#include "infrasound_frame.h"
#include <SoftwareSerial.h>

#define MYPORT_TX 14 // d5
#define MYPORT_RX 12 // d6

// Set to 0 to silence the USB debug console.
#define SENSOR_DEBUG_PRINT 1

EspSoftwareSerial::UART esp_serial;

ESP8266Timer ITimer;

SDP600 sensor;

// Written by the ISR, read by loop(). tick_ms is the intended sample instant,
// captured in the interrupt rather than whenever loop() gets round to reading
// the sensor. A missed tick therefore shows up as a 40 ms gap in the data
// instead of silently compressing time.
volatile uint32_t tick_ms;
volatile bool poll_sensor;

uint32_t nan_reads;

void IRAM_ATTR TimerHandler() {
  tick_ms = millis();
  poll_sensor = true;
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    yield();
  }
  delay(100);

  esp_serial.begin(infrasound::LINK_BAUD, SWSERIAL_8N1, MYPORT_RX, MYPORT_TX,
                    false);
  sensor.begin();

  nan_reads = 0;

  if (!ITimer.attachInterrupt(TIMER_FREQUENCY_HZ, TimerHandler)) {
    Serial.println("Starting Timer failed!");
  }
}

void loop() {
  // Snapshot the flag and its timestamp together, before the I2C read. The
  // previous code cleared poll_sensor after sensor.read(), so a tick firing
  // during the read was silently swallowed.
  noInterrupts();
  const bool due = poll_sensor;
  const uint32_t t = tick_ms;
  poll_sensor = false;
  interrupts();

  if (!due) {
    return;
  }

  const float measurement = sensor.read();

  // SDP600::read() returns NAN when the I2C CRC fails. A failed read is a
  // missing sample: not transmitting it keeps the file free of non-finite
  // values, and the gap is recorded honestly by the timestamps.
  if (isnan(measurement)) {
    ++nan_reads;
#if SENSOR_DEBUG_PRINT
    Serial.print("nan_reads ");
    Serial.println(nan_reads);
#endif
    return;
  }

  uint8_t frame[infrasound::FRAME_SIZE];
  infrasound::encodeFrame(measurement, t, frame);
  esp_serial.write(frame, infrasound::FRAME_SIZE);

#if SENSOR_DEBUG_PRINT
  Serial.print(t);
  Serial.print(" ");
  Serial.println(measurement);
#endif
}
