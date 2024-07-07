
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
#include <cstdint>
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
FsFile timestamp_file;
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
bool is_measurement_file_free = true;

// Some global variables and buffers
uint64_t start_timestamp;
const uint32_t measurement_buffer_size = 64;
float measurements_buffer[measurement_buffer_size];
uint64_t timestamps_buffer[measurement_buffer_size];
uint32_t buffer_idx = 0;
String timestamp_file_name;
String measurement_file_name;
uint32_t ms_returned_already = 0;
uint32_t measurements_returned_already = 0;

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
  cout << "Loaded WiFi credentials for ssid: " << ssid << endl;
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

// buffer_size must be at least 30 bytes large
int generateMeasurementJson(uint8_t *buffer, size_t buffer_size,
                            uint32_t number_of_measurements,
                            uint32_t next_start_idx) {
  size_t bytes_in_buffer = 0;
  if (ms_returned_already == 0) {
    memcpy(buffer, "{\n\"ms\":[", 8);
    bytes_in_buffer += 8;
  }

  char number_buffer[21]; // uint64_t can have 20 digits
  // Writing timestamps into buffer
  if (ms_returned_already < number_of_measurements) {
    uint64_t ms;
    while (ms_returned_already < number_of_measurements) {
      if (timestamp_file.read(&ms, 8) < 8) {
        // there are no more timestamps in the file
        cout << "WARNING: File ran out of timestamps" << endl;
      }
      // convert ms into asci
      size_t string_start_idx = sizeof(number_buffer) - 1;
      number_buffer[string_start_idx] = '\0';
      do {
        number_buffer[--string_start_idx] =
            ms % 10 + '0'; // add '0' = 48 in ascii
        ms /= 10;
      } while (ms != 0);
      size_t digit_count = sizeof(number_buffer) - 1 - string_start_idx;
      memcpy(buffer + bytes_in_buffer, number_buffer + string_start_idx,
             digit_count);
      bytes_in_buffer += digit_count;

      ++ms_returned_already;
      if (ms_returned_already == number_of_measurements) {
        buffer[bytes_in_buffer++] = ']';
        buffer[bytes_in_buffer++] = ',';
        buffer[bytes_in_buffer++] = '\n';
      } else {
        buffer[bytes_in_buffer++] = ',';
        if (buffer_size - bytes_in_buffer < 23) {
          // potentially not enough space in buffer for next timestamp
          return bytes_in_buffer;
        }
      }
    }
  }
  // At this point it is guaranteed that the part of the json with the "ms" is
  // completely written to the buffer
  if (buffer_size - bytes_in_buffer < 23) {
    return bytes_in_buffer;
  }
  if (measurements_returned_already == 0) {
    memcpy(buffer + bytes_in_buffer, "\"preassure\":[", 13);
    bytes_in_buffer += 13;
  }
  float preassure;
  while (measurements_returned_already < number_of_measurements) {
    if (measurement_file.read(&preassure, 4) < 4) {
      // there are no more timestamps in the file
      cout << "WARNING: File ran out of measurements" << endl;
    }
    // convert preassure into asci - left aligned string
    dtostrf(preassure, -1, 7, number_buffer);
    uint32_t digits = strlen(number_buffer);
    memcpy(buffer + bytes_in_buffer, number_buffer, digits);
    bytes_in_buffer += digits;
    ++measurements_returned_already;
    if (measurements_returned_already == number_of_measurements) {
      buffer[bytes_in_buffer++] = ']';
      buffer[bytes_in_buffer++] = ',';
      buffer[bytes_in_buffer++] = '\n';
    } else {
      buffer[bytes_in_buffer++] = ',';
      if (buffer_size - bytes_in_buffer < 23) {
        // potentially not enough space in buffer for next measurement
        return bytes_in_buffer;
      }
    }
  }

  if (bytes_in_buffer == 0) {
    measurements_returned_already = 0;
    ms_returned_already = 0;
    measurement_file.close();
    timestamp_file.close();
    is_measurement_file_free = true;
    return bytes_in_buffer;
  }

  if (bytes_in_buffer - buffer_size < ...) {
    return bytes_in_buffer;
  }
  memcpy(buffer, "next_start_idx:" , 15);
  bytes_in_buffer += 15;
  // convert next_start_idx into asci
  size_t string_start_idx = sizeof(number_buffer) - 1;
  number_buffer[string_start_idx] = '\0';
  do {
    number_buffer[--string_start_idx] =
        next_start_idx % 10 + '0'; // add '0' = 48 in ascii
    ms /= 10;
  } while (next_start_idx != 0);
  size_t digit_count = sizeof(number_buffer) - 1 - string_start_idx;
  memcpy(buffer + bytes_in_buffer, number_buffer + string_start_idx,
         digit_count);
  bytes_in_buffer += digit_count;

  return bytes_in_buffer;
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

  if (!is_measurement_file_free) {
    cout << "WARNING: file \'" << measurement_file_name
         << "\' is not free! Not continuing" << endl;
    request->send(500);
    return;
  }
  is_measurement_file_free = false;
  // Open files with timestamps and measurements
  if (!timestamp_file.open(timestamp_file_name.c_str(), O_RDONLY)) {
    cout << "Failed to open timestamp file: " << timestamp_file_name << endl;
    request->send(500);
    return;
  }
  if (!measurement_file.open(measurement_file_name.c_str(), O_RDONLY)) {
    cout << "Failed to open measurement file: " << measurement_file_name
         << endl;
    timestamp_file.close();
    request->send(500);
    return;
  }

  // seek to starting point in files such that mx_length timestamp/measurement
  // pairs are returned
  uint32_t n_measurements_in_file = (uint32_t)measurement_file.fileSize() / 4;
  max_length = min(max_length, n_measurements_in_file);
  uint32_t seek_to_measurement =
      max(start_with_idx, n_measurements_in_file - max_length);
  // recompute how many measurements should be read:
  max_length = n_measurements_in_file - seek_to_measurement;
  uint32_t next_start_idx = start_with_idx + max_length;
  // seek accordingly
  uint32_t bytes_to_seek_timestamp = seek_to_measurement * 8;
  uint32_t bytes_to_seek_measurement = seek_to_measurement * 4;

  if (!timestamp_file.seek(bytes_to_seek_timestamp)) {
    cout << "Failed to seek back " << bytes_to_seek_timestamp << " bytes in "
         << timestamp_file_name << endl;
    timestamp_file.close();
    measurement_file.close();
    is_measurement_file_free = true;
    request->send(500);
    return;
  }
  if (!measurement_file.seek(bytes_to_seek_measurement)) {
    cout << "Failed to seek back " << bytes_to_seek_measurement << " bytes in "
         << measurement_file_name << endl;
    timestamp_file.close();
    measurement_file.close();
    is_measurement_file_free = true;
    request->send(500);
    return;
  }

  AsyncWebServerResponse *response = request->beginChunkedResponse(
      "application/json",
      [max_length = max_length, next_start_idx = next_start_idx](uint8_t *buffer, size_t maxLen,
                                size_t index) -> size_t {
        // Write up to "maxLen" bytes into "buffer" and return the amount
        // written. index equals the amount of bytes that have been already
        // sent You will be asked for more data until 0 is returned Keep in
        // mind that you can not delay or yield waiting for more data!
        return generateMeasurementJson(buffer, maxLen, max_length, next_start_idx);
      });
  response->addHeader("InfrasoundSensor", "ESP Infrasound sensor webserver");
  request->send(response);
}

void onGetDownloads(AsyncWebServerRequest *request) {
  // TODO!!!
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

  // send 128 bytes as html text
  AsyncWebServerResponse *response = request->beginChunkedResponse(
      contentType.c_str(),
      [ssd_path = ssd_path](uint8_t *buffer, size_t maxLen,
                            size_t index) -> size_t {
        if (!html_file.open(ssd_path.c_str(), O_RDONLY)) {
          cout << "Failed to open html_file" << endl;
          return 0;
        }
        if (!html_file.seek(index)) {
          cout << "Failed to seek in html_file" << endl;
        }
        size_t read_bytes = html_file.read(buffer, maxLen);
        html_file.close();
        return read_bytes;
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
  measurement_file_name = "/measurements/" + String(start_timestamp);
  timestamp_file_name = "/measurements/" + String(start_timestamp) + "_ms";
  uint8_t retries = 0;
  while (sd.exists(measurement_file_name.c_str())) {
    ++retries;
    measurement_file_name =
        "/measurements/" + String(start_timestamp) + "_" + String(retries);
    timestamp_file_name = measurement_file_name + "_ms";
  }
  cout << "Creating file " << measurement_file_name << " and "
       << timestamp_file_name << endl;
  if (!measurement_file.open(measurement_file_name.c_str(),
                             O_WRONLY | O_CREAT | O_TRUNC)) {
    cout << "Failed to create file " << measurement_file_name << endl;
  }
  if (!timestamp_file.open(timestamp_file_name.c_str(),
                           O_WRONLY | O_CREAT | O_TRUNC)) {
    cout << "Failed to create file " << timestamp_file_name << endl;
  }
  measurement_file.close();
  timestamp_file.close();
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

  createMeasurementFile();

  // Setup Webserver
  initWebserver();

  // Setup Timers based on enabled features
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

bool openMeasurementFileWriting() {
  if (!timestamp_file.open(timestamp_file_name.c_str(),
                           O_WRONLY | O_APPEND | O_AT_END)) {
    cout << "Failed to open timestamp file: " << timestamp_file_name << endl;
    return false;
  }
  if (!measurement_file.open(measurement_file_name.c_str(),
                             O_WRONLY | O_APPEND | O_AT_END)) {
    cout << "Failed to open measurement file: " << measurement_file_name
         << endl;
    timestamp_file.close();
    return false;
  }
  return true;
}

void write_buffers_to_disk() {
  while (!is_measurement_file_free) {
    if (buffer_idx < measurement_buffer_size) {
      return; // just keep it in the buffer and write it next time
    } else {
      yield(); // buffer is full, wait for it to be empty
    }
  }
  is_measurement_file_free = false;
  if (!openMeasurementFileWriting()) {
    return;
  }
  // Write buffers
  for (uint32_t i = 0; i < buffer_idx; ++i) {
    if (timestamp_file.write((uint8_t *)&timestamps_buffer[i], 8) != 8) {
      cout << "Writing timestamp failed" << endl;
    }
    if (measurement_file.write((uint8_t *)&measurements_buffer[i], 4) != 4) {
      cout << "Writing measurement failed" << endl;
    }
  }
  timestamp_file.close();
  measurement_file.close();
  buffer_idx = 0;
  is_measurement_file_free = true;
}

void store_measurement(const uint64_t &timestamp, float measurement) {
  timestamps_buffer[buffer_idx] = timestamp;
  measurements_buffer[buffer_idx] = measurement;
  ++buffer_idx;
  write_buffers_to_disk();
}

void loop() {
  if (poll_sensor_now) {
    uint64_t timestamp = start_timestamp + millis();
    // TODO read infrasound sensor
    // float measurement = sensor.read();
    float measurement = 0.42f;
    store_measurement(timestamp, measurement);
    poll_sensor_now = false;
  }
}
