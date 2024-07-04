#include <Arduino.h>
#include <AsyncJson.h>
#include <AsyncTCP.h>
#include <ESP8266TimerInterrupt.h>
#include <ESP8266WiFi.h>
#include <ESP8266_ISR_Timer.h>
#include <ESP8266_ISR_Timer.hpp>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <NTPClient.h>
#include <SdFat.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <cstdint>
#include <future>
#include <iostream/ArduinoStream.h>

#include "SDP600.h"
#include "webserver_endpoints.h"

#define SPI_SPEED SD_SCK_MHZ(4)

String ssid;
String password;
AsyncWebServer server(80);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");
const char *wifi_acces_file = "wifi_ssid_pw.txt";

SDP600 sensor;

// Timers
#define HW_TIMER_INTERVAL_MS 1L
ESP8266Timer ITimer;                // hardware timer
ESP8266_ISR_Timer ISR_Timer;        // virtual timers
uint32_t time_interval_sensor = 20; // poll sensor every 20 ms

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
bool is_webserver_available = false;
bool is_wifi_client = false;

// Some global variables and buffers
uint64_t start_timestamp;
const uint32_t buffer_size = 1024;
float measurements_buffer[buffer_size];
uint64_t timestamps_buffer[buffer_size];
uint32_t buffer_idx = 0;

void reformatMsg() {
  cout << F("Try reformatting the card.  For best results use\n");
  cout << F("the SdFormatter program in SdFat/examples or download\n");
  cout << F("and use SDFormatter from www.sdcard.org/downloads.\n");
}

void hardwareTimerHandler() { ISR_Timer.run(); }
void pollSensorISR() {
  cout << "DEBUG: Polling Sensor ISR" << endl;
  poll_sensor_now = true;
}

bool initSdCard() {
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
  return false;
}

bool load_wifi_credentials() {
  cout << "Loading WiFi credentials from LittleFS" << endl;
  if (!LittleFS.begin()) {
    cout << "Could not initialize LittleFS" << endl;
    return false;
  }
  File wifi_credential_file = LITTLEFS.open(wifi_acces_file, "r");
  if (!wifi_credential_file) {
    cout << "WiFi credential file does not exist: " << wifi_credential_file
         << endl;
    return false
  }
  cout << "Loading WiFi credentials" << endl;
  ssid = wifi_credential_file.readStringUntil('\n').c_str();
  password = wifi_credential_file.readStringUntil('\n').c_str();
  wifi_credential_file.close() cout << "Loading WiFi credentials" << endl;
  if (ssid_str.length() < 1 || password_str.length() < 1) {
    cout << "Credential file doesn't contain ssid and password" << endl;
    return false;
  }
  cout << "Loaded WiFi credentials for ssid " << ssid << endl;
}
bool write_wifi_credentials() {
  cout << "Writing WiFi credentials for ssid:" << ssid << endl;
  File wifi_credential_file = LITTLEFS.open(wifi_credential_file, "w");
  // TODO checks necessary?
  wifi_credential_file.println(ssid);
  wifi_credential_file.println(password);
  file.close();
  return true;
}

bool initWebserver() {
  if (is_wifi_client) {
    server.serveStatic("/", SPIFFS, "/www/static/");
  } else {
    server.serveStatic("/", SPIFFS, "/www/static/").setDefaultFile("wifi.html");
  }
  server.onNotFound(notFound);
  server.on("/measurements", HTTP_GET, onGetMeasurement);
  server.on("/downloads", HTTP_GET, onGetDownloads);
  server.on("/set_wifi", HTTP_POST, onPostWifi);
}

void configureAccessPoint() {
  while (!WiFi.softAPConfig(local_IP, gateway, subnet)) {
    cout << "Failed - trying again" << endl;
  }
  cout << "Setting up access point 'infrasound-sensor'" << endl;
  while (!WiFi.softAP("infrasound-sensor")) {
    cout << "Failed - trying again" << endl;
  }
  cout << "Access point was set up." << endl;
  cout << "SSID: 'infrasound-sensor', no password, IP-Address:"
       << WiFi.softAPIP() << endl;
}

void initWifi() {
  // Try loading wifi credentials
  bool successfully_read_wifi_credentials = load_wifi_credentials();
  if (successfully_read_wifi_credentials) {
    // establish connection
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
  }
  if (WiFi.waitForConnectionResult() != WL_CONNECTED) {
    cout << "Failed to connect to: " << ssid << endl;
    cout << "Configuring own Networ Access Point" << endl;
    configureAccessPoint();
    return false;
  }
  cout << "IP Address: " << WiFi.localIP() << endl;
  is_wifi_client = true;
}

void notFound(AsyncWebServerRequest *request) {
  request->send(404, "text/plain", "Not found");
}

void onGetMeasurement(AsyncWebServerRequest *request) {
  uint32_t start_with_idx, max_length;
  if (request->hasParam("start_with_idx")) {
    String start_with_idx_str = request->getParam("start_with_idx")->value();
    start_with_idx = strtoul(start_with_idx_str);
  } else {
    start_with_idx = 0;
  }
  if (request->hasParam("max_length")) {
    String max_length_str = request->getParam("max_length")->value();
    max_length = strtoul(max_length_str);
  } else {
    max_length = 5000;
  }
  AsyncJsonResponse *response = new AsyncJsonResponse();
  response->addHeader("InfrasoundSensor", "ESP Infrasound sensor webserver");
  JsonObject &root = response->getRoot();
  uint32_t next_start_idx = 42; // TODO
  root["next_start_idx"] = next_start_idx;
  root["ms"] = {1, 2, 3, 4, 5, 6};                        // TODO
  root["preassure"] = {-2.0f, -1.0f, 0.f, 1.f, 2.f, 3.f}; // TODO
  respon->setLength();
  request->send(response);
}

void onGetDownloads(AsyncWebServerRequest *request) {
  AsyncJsonResponse *response = new AsyncJsonResponse();
  response->addHeader("InfrasoundSensor", "ESP Infrasound sensor webserver");
  JsonObject &root = response->getRoot();
  root["download-links"] = "download-links";
  root["link-texts"] = "link-texts";
  response->setLength();
  request->send(response);
}

void onPostWifi(AsyncWebServerRequest *request) {
  if (request->hasParam("ssid") && request->hasParam("password")) {
    ssid = request->getParam("ssid")->value();
    password = request->getParam("ssid")->value();
    if (write_wifi_credentials()) {
      cout << "WiFi credentials successfully updated" << endl;
    } else {
      cout << "Couldn't write wif credentials" << endl;
    }
    // shutdown current webserver
    cout << "Trying to connect to new WiFi: " << ssid << endl;
    server.end();
    if (is_wifi_client) {
      WiFi.end();
    } else {
      WiFi.softAPdisconnect(true);
    }
    initWifi();
    initWebserver();
  }
}

void initTimestamp() {
  cout << "Initializing start-timestamp" << endl;
  timeClient.begin();
  timeClient.update();
  time_t epochTime = timeClient.getEpochTime();
  start_timestamp = epochTime * 1000 - millis();
  cout << "Starttimestamp is " << start_timestamp << " ms" << endl;
}

void setup() {
  Serial.begin(38400);
  while (!Serial) {
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
  cout << "--------------------------" << endl;

  // Initialize SD-Card
  is_sd_card_available = initSdCard();

  // init wifi
  initWifi();
  // init time
  if (is_wifi_client) {
    initTimestamp();
  }

  // Setup Webserver
  is_webserver_available = initWebserver();

  // Setup Timers based on enabled features
  cout << "Initializing Timers" << endl;
  cout << "CPU Frequency = " << F_CPU / 1000000 << endl;
  cout << " MHz" << endl;
  // Hardware interval = 1ms set in microsecs
  if (ITimer.attachInterruptInterval(1 * 1000, hardwareTimerHandler)) {
    ISR_Timer.setInterval(time_interval_sensor, pollSensorISR);
  } else {
    cout << "Can't set ITimer correctly. Select another freq. or interval"
         << endl;
  }
}

void openMeasurementFile() {
  if (!is_sd_card_available) {
    cout
        << "Error: Couldn't write measurements to disk - SD card not available."
        << endl;
    return;
  }
  if (!sd.exists("/measurements")) {
    cout << "Creating /measurements folder on sd-card" << endl;
    if (!sd.mkdir("/measurements")) {
      cout << "Error: Couldn't create /measurements directory. Check SD-Card."
           << endl;
      return;
    }
  }
  String file_name = "/measurements/" + String(start_timestamp);
  if (sd.exists(file_name)) {
    cout << file_name << " exists already." << endl;

  }
}

void write_buffers_to_disk() {
  if (!file) {
    openMeasurementFile();
  }

  void store_measurement(const &uint64_t timestamp, float measurement) {
    timestamps_buffer[buffer_idx] = timestamp;
    measurements_buffer[buffer_idx] = measurement;
    ++buffer_idx;
    if (buffer_idx) >= buffer_size) {
        write_buffers_to_disk();
        buffer_idx = 0;
      }
  }

  void loop() {
    if (poll_sensor_now) {
      cout << "TODO: poll sensor here" << endl;
      uint64_t timestamp = start_timestamp + millis();
      float measurement = sensor.read();
      store_measurement(timestamp, measurement);
      poll_sensor_now = false;
    }
  }
