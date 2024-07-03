#include <Arduino.h>
#include <ESP8266TimerInterrupt.h>
#include <ESP8266_ISR_Timer.hpp>  
#include <ESP8266WiFi.h>
#include <ESP8266_ISR_Timer.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SdFat.h>
#include <iostream/ArduinoStream.h>

#include "SDP600.h"

#define SPI_SPEED SD_SCK_MHZ(4)

AsyncWebServer server(80);

SDP600 sensor;

// Timers
#define HW_TIMER_INTERVAL_MS 1L
ESP8266Timer ITimer;         // hardware timer
ESP8266_ISR_Timer ISR_Timer; // virtual timers
uint32_t time_interval_sensor = 20; // poll sensor every 20 ms
uint32_t time_interval_disk = 60000; // write to disk every 60 sec

// SSD Chip Select pin
const int sd_chip_select = 8;
SdFs sd;
FsFile file;

// Printing and reading from USB with cout and cin
ArduinoOutStream cout(Serial);
// input buffer for line
char cinBuf[64];
ArduinoInStream cin(Serial, cinBuf, sizeof(cinBuf));

// Some global variables to enable or disable features based on connected
// hardware
bool is_sd_card_available = false;
bool poll_sensor_now = false;
bool write_to_disk_now = false;

void reformatMsg() {
  cout << F("Try reformatting the card.  For best results use\n");
  cout << F("the SdFormatter program in SdFat/examples or download\n");
  cout << F("and use SDFormatter from www.sdcard.org/downloads.\n");
}


void hardwareTimerHandler() {
  ISR_Timer.run();
}
void pollSensorISR() {
  cout << "DEBUG: Polling Sensor ISR" << endl;
  poll_sensor_now = true;
}
void writeToDiskISR() {
  cout << "DEBUG: Writing to Disk ISR" << endl;
  write_to_disk_now = true;
}

void setup() {
  Serial.begin(38400);
  while (!Serial ) {
    yield;
  }
  delay(500);

  // Print greeting message
  cout << endl;
  cout << "Starting Infrasound Sensor" << endl;
  cout << ARDUINO_BOARD << endl;
  cout << "##########################" << endl;

  // Initialize Sensor
  cout << "Initializing Sensor" << endl;
  sensor.begin();
  sensor.setResolution(14);
  cout << "Sensor initialized" << endl;
  cout <<"--------------------------" << endl;

  // Initialize SD-Card
  cout << F("\nSPI pins:\n");
  cout << F("MISO: ") << int(MISO) << endl;
  cout << F("MOSI: ") << int(MOSI) << endl;
  cout << F("SCK:  ") << int(SCK) << endl;

#if false
  if (!sd.begin(sd_chip_select, SPI_SPEED)) { // this crashes the board - try with connected sd module!
    if (sd.card()->errorCode()) {
      cout << F(
          "\nSD initialization failed.\n"
          "Do not reformat the card!\n"
          "Is the card correctly inserted?\n"
          "Is chipSelect set to the correct value?\n"
          "Does another SPI device need to be disabled?\n"
          "Is there a wiring/soldering problem?\n");
      cout << F("\nerrorCode: ") << hex << showbase;
      cout << int(sd.card()->errorCode());
      cout << F(", errorData: ") << int(sd.card()->errorData());
      cout << dec << noshowbase << endl;
      return;
    }
    cout << F("\nCard successfully initialized.\n");
   if (sd.vol()->fatType() == 0) {
      cout << F("Can't find a valid FAT16/FAT32/exFAT partition.\n");
      reformatMsg();
      return;
    }
    cout << F("Can't determine error type\n");
    return;
  }
  cout << F("\nCard successfully initialized.\n");
  cout << endl;
  is_sd_card_available = true;
#endif
  cout << "TODO" << endl;
  cout << "--------------------------" << endl;

  // Setup Timers based on enabled features
  cout << "Initalizing Timers" << endl;
  cout << "CPU Frequency = " << F_CPU / 1000000 << endl;
  cout << " MHz" <<endl;
  // Hardware interval = 1ms set in microsecs
  if (ITimer.attachInterruptInterval(1 * 1000, hardwareTimerHandler)) {
    ISR_Timer.setInterval(time_interval_sensor, pollSensorISR);
    if (is_sd_card_available) {
      ISR_Timer.setInterval(time_interval_disk, writeToDiskISR);
    }
  } else {
    cout 
    << "Can't set ITimer correctly. Select another freq. or interval" 
    << endl;
  }
}


void loop() {
  if (poll_sensor_now) {
    cout << "TODO: poll sensor here" << endl;  
    poll_sensor_now = false;
  }
  if (write_to_disk_now) {
    cout << "TODO: write to disk here" << endl;
    write_to_disk_now = false;
  }
}
