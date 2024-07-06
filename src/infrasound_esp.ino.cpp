#include <Arduino.h>
#include <AsyncJson.h>
#include <ESP8266TimerInterrupt.h>
#include <ESP8266WiFi.h>
#include <ESP8266_ISR_Timer.h>
#include <ESP8266_ISR_Timer.hpp>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <NTPClient.h>
#include <SdFat.h>
#include <WiFiUdp.h>
#include <iostream/ArduinoStream.h>

#include "SDP600.h"

#define SPI_SPEED SD_SCK_MHZ(4)

String ssid;
String password;
AsyncWebServer server(80);

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");
const char *wifi_acces_file_path = "wifi_ssid_pw.txt";

SDP600 sensor;

// Timers
#define HW_TIMER_INTERVAL_MS 1L
ESP8266Timer ITimer;                // hardware timer
ESP8266_ISR_Timer ISR_Timer;        // virtual timers
uint32_t time_interval_sensor = 20; // poll sensor every 20 ms

// SSD Chip Select pin
const int sd_chip_select = SS;
SdFs sd;
FsFile measurement_file;
FsFile html_file;

// Printing and reading from USB with cout and cin
ArduinoOutStream cout(Serial);
// input buffer for line
char cinBuf[32];
ArduinoInStream cin(Serial, cinBuf, sizeof(cinBuf));

// Some global variables to enable or disable features based on connected
// hardware
bool is_sd_card_available = false;
bool poll_sensor_now = false;
bool is_wifi_client = false;

// Some global variables and buffers
uint64_t start_timestamp;
const uint32_t buffer_size = 32;
float measurements_buffer[buffer_size];
uint64_t timestamps_buffer[buffer_size];
uint32_t buffer_idx = 0;
String file_name;

IPAddress local_IP(192, 168, 4, 1);
IPAddress gateway(192, 168, 4, 1);
IPAddress subnet(255, 255, 255, 0);

void initWifi();
void initWebserver();

void reformatMsg() {
  cout << F("Try reformatting the card.  For best results use\n");
  cout << F("the SdFormatter program in SdFat/examples or download\n");
  cout << F("and use SDFormatter from www.sdcard.org/downloads.\n");
}

void hardwareTimerHandler() { ISR_Timer.run(); }
void pollSensorISR() { poll_sensor_now = true; }

bool initSdCard() {
  cout << "Initializing SD card" << endl;
  if (!sd.begin(sd_chip_select, SPI_SPEED)) { // this crashes the board - try
                                              // with connected sd module!
    if (sd.card()->errorCode()) {
      cout << F("\nSD initialization failed.\n"
                "Do not reformat the card!\n"
                "Is the card correctly inserted?\n"
                "Is chipSelect set to the correct value?\n"
                "Does another SPI device need to be disabled?\n"
                "Is there a wiring/soldering problem?\n");
      cout << F("\nerrorCode: ") << hex << showbase;
      cout << int(sd.card()->errorCode());
      cout << F(", errorData: ") << int(sd.card()->errorData());
      cout << dec << noshowbase << endl;
      return false;
    }
    cout << F("\nCard successfully initialized.\n");
    if (sd.vol()->fatType() == 0) {
      cout << F("Can't find a valid FAT16/FAT32/exFAT partition.\n");
      reformatMsg();
      return false;
    }
    cout << F("Can't determine error type\n");
    return false;
  }
  cout << F("\nCard successfully initialized.\n");
  cout << endl;
  is_sd_card_available = true;
  return true;
}

bool load_wifi_credentials() {
  cout << "Loading WiFi credentials from SDCard" << endl;
  FsFile wifi_credential_file;
  if (!wifi_credential_file.open(wifi_acces_file_path, O_RDONLY)) {
    cout << "WiFi credential file does not exist: " << wifi_credential_file
         << endl;
    return false;
  }
  cout << "Loading WiFi credentials" << endl;
  char buffer;
  // read ssid
  ssid = "";
  while (wifi_credential_file.read(&buffer, 1) >= 1) {
    if (buffer == '\n') {
      break;
    }
    ssid += buffer;
  }
  // read password
  password = "";
  while (wifi_credential_file.read(&buffer, 1) >= 1) {
    if (buffer == '\n') {
      break;
    }
    password += buffer;
  }
  wifi_credential_file.close();
  cout << "Loaded WiFi credentials: " << ssid << ", " << password << endl;
  return true;
}

bool write_wifi_credentials() {
  cout << "Writing WiFi credentials for ssid:" << ssid << endl;
  FsFile wifi_credential_file;
  if (!wifi_credential_file.open(wifi_acces_file_path, O_WRONLY | O_CREAT)) {
    return false;
  }
  wifi_credential_file.print(ssid + "\n" + password + "\n");
  wifi_credential_file.close();
  return true;
}

void onNotFound(AsyncWebServerRequest *request) {
  cout << "A unknown request was sent" << endl;
  request->send(404, "text/plain", "Not found");
}

void onGetMeasurement(AsyncWebServerRequest *request) {
  cout << "/measurements were requested" << endl;
  uint32_t start_with_idx, max_length;
  if (request->hasParam("start_with_idx")) {
    String start_with_idx_str = request->getParam("start_with_idx")->value();
    char *end_ptr;
    start_with_idx = strtoul(start_with_idx_str.c_str(), &end_ptr, 10);
  } else {
    cout << "INFO: /measurement request doesn't have start_with_idx" << endl;
    start_with_idx = 0;
  }
  if (request->hasParam("max_length")) {
    String max_length_str = request->getParam("max_length")->value();
    char *end_ptr;
    max_length = strtoul(max_length_str.c_str(), &end_ptr, 10);
  } else {
    cout << "WARNING: /measurement request doesn't have max_length" << endl;
    max_length = 5000;
  }
  AsyncJsonResponse *response = new AsyncJsonResponse();
  response->addHeader("InfrasoundSensor", "ESP Infrasound sensor webserver");
  JsonObject root = response->getRoot();
  uint32_t next_start_idx = 42; // TODO
  root["next_start_idx"] = next_start_idx;
  JsonArray ms_data = root["ms"].to<JsonArray>();
  JsonArray preassure_data = root["preassure"].to<JsonArray>();
  for (int i = 0; i < 5; i++) {
    ms_data.add(i);
    preassure_data.add(42);
  }
  response->setLength();
  cout << "sending response" << endl;
  request->send(response);
}

void onGetDownloads(AsyncWebServerRequest *request) {
  cout << "/downloads were requested" << endl;
  AsyncJsonResponse *response = new AsyncJsonResponse();
  response->addHeader("InfrasoundSensor", "ESP Infrasound sensor webserver");
  JsonObject root = response->getRoot();
  JsonArray file_list = root["files"].to<JsonArray>();

  file_list.add("testfile");
  file_list.add("anothertestfile");
  file_list.add("last_test_file");

  response->setLength();
  request->send(response);
}

void onStaticFile(AsyncWebServerRequest *request) {
  String url = request->url();
  String contentType;
  if (url.endsWith(".html"))
    contentType = "text/html";
  else if (url.endsWith(".js"))
    contentType = "application/javascript";
  else if (url.endsWith(".wasm"))
    contentType = "application/wasm";
  else if (url.endsWith(".ico"))
    contentType = "image/x-icon";
  else
    contentType = "text/plain";

  String ssd_path = "/www/static" + url;

  html_file.close();
  if (!html_file.open(ssd_path.c_str(), O_RDONLY)) {
    request->send(200, "text/plain", "Failed to open file: " + ssd_path);
    return;
  }
  // send 128 bytes as html text
  AsyncWebServerResponse *response = request->beginChunkedResponse(
      contentType.c_str(),
      [](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
        // Write up to "maxLen" bytes into "buffer" and return the amount
        // written. index equals the amount of bytes that have been already
        // sent You will be asked for more data until 0 is returned Keep in
        // mind that you can not delay or yield waiting for more data!
        return html_file.read(buffer, maxLen);
      });
  response->addHeader("InfrasoundSensor", "ESP Infrasound sensor webserver");
  request->send(response);
}

void onIndex(AsyncWebServerRequest *request) {
  String RedirectUrl = "http://";
  if (ON_STA_FILTER(request)) {
    RedirectUrl += WiFi.localIP().toString();
    RedirectUrl += "/index.html";
  } else {
    RedirectUrl += WiFi.softAPIP().toString();
    RedirectUrl += "/wifi.html";
  }
  request->redirect(RedirectUrl);
}

void onPostWifi(AsyncWebServerRequest *request) {
  cout << "/set_wifi credentials were sent" << endl;
  int params = request->params();
  for (int i = 0; i < params; i++) {
    AsyncWebParameter *p = request->getParam(i);
    if (p->isPost()) {
      if (p->name() == "ssid") {
        ssid = p->value();
      } else if (p->name() == "password") {
        password = p->value();
      }
    }
  }
  cout << "DEBUG: SSID: " << ssid << " Password: " << password << endl;
  if (ssid.length() == 0) {
    cout << "SSID length is 0... ignoring data" << endl;
    request->send(200);
    return;
  }
  if (write_wifi_credentials()) {
    cout << "WiFi credentials successfully updated ... restarting" << endl;
    ESP.restart();
  } else {
    cout << "Couldn't write WiFi credentials" << endl;
    request->send(200);
  }
}

void initWebserver() {

  cout << "Serving static files" << endl;
  server.on("/", HTTP_GET, onIndex);
  server.on("/index.html", HTTP_GET, onStaticFile);
  server.on("/downloads.html", HTTP_GET, onStaticFile);
  server.on("/wifi.html", HTTP_GET, onStaticFile);
  server.on("/download_client.js", HTTP_GET, onStaticFile);
  server.on("/infrasound_client.js", HTTP_GET, onStaticFile);
  server.on("/favicon.ico", HTTP_GET, onStaticFile);
  server.on("/pffft/pffft.js", HTTP_GET, onStaticFile);
  server.on("/pffft/pffft.wasm", HTTP_GET, onStaticFile);

  cout << "Serving /measurements" << endl;
  server.on("/measurements", HTTP_GET, onGetMeasurement);
  cout << "serving /downloads" << endl;
  server.on("/downloads", HTTP_GET, onGetDownloads);
  cout << "serving /set_wifi" << endl;
  server.on("/set_wifi", HTTP_POST, onPostWifi);
  server.onNotFound(onNotFound);

  server.begin();
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
       << WiFi.softAPIP().toString() << endl;
}

void initWifi() {
  // Try loading wifi credentials
  bool successfully_read_wifi_credentials = load_wifi_credentials();
  if (successfully_read_wifi_credentials) {
    // establish connection
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
  }
  if (WiFi.waitForConnectResult() != WL_CONNECTED) {
    cout << "Failed to connect to: " << ssid << endl;
    cout << "Configuring own Network Access Point" << endl;
    configureAccessPoint();
    is_wifi_client = false;
    return;
  }
  cout << "IP Address: " << WiFi.localIP().toString() << endl;
  is_wifi_client = true;
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
  Serial.begin(115200);
  while (!Serial) {
    delay(1);
  }
  delay(500);

  // Print greeting message
  cout << endl;
  cout << "Starting Infrasound Sensor" << endl;
  cout << ARDUINO_BOARD << endl;
  cout << "##########################" << endl;

  if (!LittleFS.begin()) {
    cout << "Could not initialize LittleFS" << endl;
    return;
  }
  LittleFS.format();
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
  initWebserver();

  // Setup Timers based on enabled features
  /*  cout << "Initializing Timers" << endl;
    cout << "CPU Frequency = " << F_CPU / 1000000 << endl;
    cout << " MHz" << endl;
    // Hardware interval = 1ms set in microsecs
    if (ITimer.attachInterruptInterval(1 * 1000, hardwareTimerHandler)) {
      ISR_Timer.setInterval(time_interval_sensor, pollSensorISR);
    } else {
      cout << "Can't set ITimer correctly. Select another freq. or interval"
           << endl;
    }
    */
}

void createMeasurementFile() {
  if (!is_sd_card_available) {
    cout << "Error: Couldn't write measurements to disk - SD card not "
            "available."
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
  file_name = "/measurements/" + String(start_timestamp);
  if (measurement_file.isOpen()) {
    return;
  }
  uint8_t retries = 0;
  while (sd.exists(file_name.c_str())) {
    ++retries;
    file_name =
        "/measurements/" + String(start_timestamp) + "_" + String(retries);
  }
  cout << "Creating file " << file_name << endl;
  if (!measurement_file.open(file_name.c_str(), O_WRONLY | O_CREAT)) {
    cout << "Failed to create file" << endl;
  }
}

void openMeasurementFile() {
  cout << "Appending to file" << endl;
  if (measurement_file.isOpen()) {
    return;
  }
  if (!measurement_file.open(file_name.c_str(),
                             O_WRONLY | O_APPEND | O_AT_END)) {
    cout << "Failed to open file " << file_name << endl;
  }
}

void write_buffers_to_disk() {
  while (!measurement_file) {
    createMeasurementFile();
  }
  openMeasurementFile();
  // Write buffers
  for (uint32_t i = 0; i < buffer_idx; ++i) {
    if (measurement_file.write((uint8_t *)&timestamps_buffer[i], 8) != 8) {
      cout << "Writing timestamp failed" << endl;
    }
    if (measurement_file.write((uint8_t *)&measurements_buffer[i], 4) != 4) {
      cout << "Writing measurement failed" << endl;
    }
  }
  measurement_file.flush();
}

void store_measurement(const uint64_t &timestamp, float measurement) {
  timestamps_buffer[buffer_idx] = timestamp;
  measurements_buffer[buffer_idx] = measurement;
  ++buffer_idx;
  if (buffer_idx >= buffer_size) {
    write_buffers_to_disk();
    buffer_idx = 0;
  }
}

void loop() {
  if (poll_sensor_now) {
    uint64_t timestamp = start_timestamp + millis();
    float measurement = sensor.read();
    store_measurement(timestamp, measurement);
    poll_sensor_now = false;
  }
}
