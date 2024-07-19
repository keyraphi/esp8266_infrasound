#include "c_types.h"
#include <Arduino.h>
#include <AsyncJson.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <NTPClient.h>
#include <SdFat.h>
#include <SoftwareSerial.h>
#include <WiFiUdp.h>
#include <circular_queue/circular_queue.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream/ArduinoStream.h>
#include <strings.h>

#define SPI_SPEED SD_SCK_MHZ(4)
#define MYPORT_TX 5
#define MYPORT_RX 4

EspSoftwareSerial::UART arduino_serial;

String ssid;
String password;
AsyncWebServer server(80);
AsyncEventSource events("/measurement_events");

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");
const char *wifi_acces_file_path = "wifi_ssid_pw.txt";

// SSD Chip Select pin
const int sd_chip_select = SS;
SdFs sd;
FsFile measurement_file;

// Printing and reading from USB with cout and cin
ArduinoOutStream cout(Serial);
// input buffer for line
char cinBuf[32];
ArduinoInStream cin(Serial, cinBuf, sizeof(cinBuf));

// Some global variables to enable or disable features based on connected
// hardware
bool is_sd_card_available = false;
bool is_wifi_client = false;
bool is_json_finalized = false;
uint64_t start_timestamp = 0;

// Some global variables and buffers
// ring buffer to buffer new measurements
circular_queue<float> measurements_buffer(32);

String measurement_file_name;
uint32_t measurements_returned_already = 0;

IPAddress local_IP(192, 168, 4, 1);
IPAddress gateway(192, 168, 4, 1);
IPAddress subnet(255, 255, 255, 0);

// forward declarations
void initWifi();
void initWebserver();
void pollSensorISR();
bool openMeasurementFileAppending();

void reformatMsg() {
  cout << F("Try reformatting the card.  For best results use\n");
  cout << F("the SdFormatter program in SdFat/examples or download\n");
  cout << F("and use SDFormatter from www.sdcard.org/downloads.\n");
}

// void hardwareTimerHandler() { ISR_Timer.run(); }

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
                            uint32_t next_start_idx,
                            uint32_t bytes_to_seek_measurement) {
  // If measurement_file are open currently... close them and
  // remember to open them before leaving this function
  bool closed_measurement_file = false;
  if (measurement_file.isOpen()) {
    measurement_file.close();
    closed_measurement_file = true;
  }
  FsFile value_file;
  if (!value_file.open(measurement_file_name.c_str(), O_RDONLY)) {
    cout << "Failed to open measurement file " << measurement_file_name << endl;
    return 0;
  }
  // Seek to the next measurements

  if (!value_file.seek(bytes_to_seek_measurement +
                       measurements_returned_already * 4)) {
    cout << "Failed to seek "
         << bytes_to_seek_measurement + measurements_returned_already * 4
         << " bytes in " << measurement_file_name << endl;
    if (closed_measurement_file) {
      openMeasurementFileAppending();
    }
    value_file.close();
    return 0;
  }

  size_t bytes_in_buffer = 0;
  char number_buffer[21];

  if (measurements_returned_already == 0) {
    memcpy(buffer + bytes_in_buffer, "{\n\"preassure\":[", 15);
    bytes_in_buffer += 15;
  }
  float preassure;
  while (measurements_returned_already < number_of_measurements) {
    if (value_file.read(&preassure, 4) < 4) {
      // there are no more timestamps in the file
      cout << "WARNING: File ran out of measurements" << endl;
      value_file.close();
      return bytes_in_buffer;
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
        value_file.close();
        if (closed_measurement_file) {
          openMeasurementFileAppending();
        }
        return bytes_in_buffer;
      }
    }
  }

  if (bytes_in_buffer == 0 && is_json_finalized) {
    measurements_returned_already = 0;
    value_file.close();
    is_json_finalized = false;
    if (closed_measurement_file) {
      openMeasurementFileAppending();
    }
    return bytes_in_buffer;
  }

  if (bytes_in_buffer - buffer_size < 39) {
    is_json_finalized = false;
    value_file.close();
    if (closed_measurement_file) {
      openMeasurementFileAppending();
    }
    return bytes_in_buffer;
  }
  value_file.close();
  memcpy(buffer + bytes_in_buffer, "\"next_start_idx\":", 17);
  bytes_in_buffer += 17;
  // convert next_start_idx into asci
  size_t string_start_idx = sizeof(number_buffer) - 1;
  number_buffer[string_start_idx] = '\0';
  do {
    number_buffer[--string_start_idx] =
        next_start_idx % 10 + '0'; // add '0' = 48 in ascii
    next_start_idx /= 10;
  } while (next_start_idx != 0);
  size_t digit_count = sizeof(number_buffer) - 1 - string_start_idx;
  memcpy(buffer + bytes_in_buffer, number_buffer + string_start_idx,
         digit_count);
  bytes_in_buffer += digit_count;

  memcpy(buffer + bytes_in_buffer, "\n}", 2);
  bytes_in_buffer += 2;
  is_json_finalized = true;

  if (closed_measurement_file) {
    openMeasurementFileAppending();
  }
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

  // Find out where to seek for the first measurement
  FsFile value_file;
  if (!value_file.open(measurement_file_name.c_str(), O_RDONLY)) {
    cout << "Failed to open measurement file: " << measurement_file_name
         << endl;
    request->send(500);
    return;
  }
  // seek to starting point in files such that mx_length timestamp/measurement
  // pairs are returned
  uint32_t n_measurements_in_file = (uint32_t)value_file.fileSize() / 4;
  max_length = min(max_length, n_measurements_in_file);
  uint32_t seek_to_measurement =
      max(start_with_idx, n_measurements_in_file - max_length);
  // recompute how many measurements should be read:
  max_length = n_measurements_in_file - seek_to_measurement;
  uint32_t next_start_idx = start_with_idx + max_length;
  // seek accordingly
  uint32_t bytes_to_seek_measurement = seek_to_measurement * 4;
  value_file.close();

  // Respond in chunks to not block the esp completely
  AsyncWebServerResponse *response = request->beginChunkedResponse(
      "application/json",
      [max_length = max_length, next_start_idx = next_start_idx,
       bytes_to_seek_measurement = bytes_to_seek_measurement](
          uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
        return generateMeasurementJson(buffer, maxLen, max_length,
                                       next_start_idx,
                                       bytes_to_seek_measurement);
      });
  response->addHeader("InfrasoundSensor", "ESP Infrasound sensor webserver");
  request->send(response);
}

void onGetDownloads(AsyncWebServerRequest *request) {
  AsyncJsonResponse *response = new AsyncJsonResponse();
  response->addHeader("InfrasoundSensor", "ESP Infrasound sensor webserver");
  JsonObject root = response->getRoot();
  JsonArray file_list = root["files"].to<JsonArray>();

  FsFile directory;
  directory.open("/measurements", O_RDONLY);
  directory.rewind();
  FsFile file;
  char file_name[21];
  while (file.openNext(&directory, O_RDONLY)) {
    if (!file.isHidden()) {
      file.getName(file_name, sizeof(file_name));
      if (strcmp(&file_name[strlen(file_name) - 2], "ms") == 0) {
        continue;
      }
      file_list.add(file_name);
    }
    file.close();
  }
  response->setLength();
  request->send(response);
}

void onDownload(AsyncWebServerRequest *request) {
  if (!request->hasParam("file")) {
    request->send(500);
  }
  String file_name = request->getParam("file")->value();

  AsyncWebServerResponse *response = request->beginChunkedResponse(
      "audio/wav",
      [file_name = file_name](uint8_t *buffer, size_t maxLen,
                              size_t index) -> size_t {
        FsFile file;
        if (!file.open(("/measurements/" + file_name).c_str(), O_RDONLY)) {
          return 0;
        }

        // seek to current position
        if (index == 0) {
          memcpy(buffer, "data", 4);
          uint32_t file_size = file.fileSize();
          memcpy(buffer + 4, reinterpret_cast<char *>(&file_size), 4);
          maxLen -= 8;
        }
        file.seek(index);
        size_t bytes_read = file.read(buffer, maxLen);
        file.close();
        return bytes_read;
      });

  char buf[28 + file_name.length()];
  memcpy(buf + 0, "attachment; filename=\"", 22);
  memcpy(buf + 22, file_name.c_str(), file_name.length());
  memcpy(buf + 22 + file_name.length(), ".raw\"\0", 6);

  response->addHeader("Content-Disposition", buf);
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
        FsFile html_file;
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

void onConnect(AsyncEventSourceClient *client) {
  if (client->lastId()) {
    cout << "Client " << client->lastId() << " has reconnected" << endl;
  } else {
  }
}

void onStartTimestamp(AsyncWebServerRequest *request) {
  AsyncResponseStream *response = request->beginResponseStream("text/plain");
  response->printf("%llu", start_timestamp);
  request->send(response);
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
  cout << "serving /download" << endl;
  server.on("/download", HTTP_GET, onDownload);
  cout << "serving /set_wifi" << endl;
  server.on("/set_wifi", HTTP_POST, onPostWifi);
  cout << "serving /start_timestamp" << endl;
  server.on("/start_timestamp", HTTP_GET, onStartTimestamp);
  server.onNotFound(onNotFound);

  cout << "Setting up handler for /measurement_event" << endl;
  events.onConnect(onConnect);
  server.addHandler(&events);

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
  uint8_t retries = 0;
  while (sd.exists(measurement_file_name.c_str())) {
    ++retries;
    measurement_file_name =
        "/measurements/" + String(start_timestamp) + "_" + String(retries);
  }
  cout << "Creating file " << measurement_file_name << endl;
  if (!measurement_file.open(measurement_file_name.c_str(),
                             O_WRONLY | O_CREAT | O_TRUNC)) {
    cout << "Failed to create file " << measurement_file_name << endl;
  }
  measurement_file.close();
}

void wait_forever() {
  cout << "Waiting forever" << endl;
  while (1) {
    delay(1000);
  }
}

void setup() {
  // Debug serial connected to USB
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

  // Connection to Arduino serial using software serial
  cout << "Connecting to Arduino board" << endl;
  arduino_serial.begin(9600, SWSERIAL_8N1, MYPORT_RX, MYPORT_TX, false);
  if (!arduino_serial) {
    cout << "Invalid EspSoftwareSerial pin configuration, check config!";
    // don't continue with broken configuration
    wait_forever();
  }
  cout << "Connection to Arduino established" << endl;

  // Initialize SD-Card
  while (!initSdCard()) {
    cout << "Failed to initialize SD-Card..." << endl;
    delay(1000);
    cout << "Trying again" << endl;
  }

  // init wifi
  initWifi();
  // init time
  if (is_wifi_client) {
    initTimestamp();
  }

  createMeasurementFile();

  // Setup Webserver
  initWebserver();
}

bool openMeasurementFileAppending() {
  if (!measurement_file.open(measurement_file_name.c_str(),
                             O_WRONLY | O_APPEND | O_AT_END)) {
    cout << "Failed to open measurement file: " << measurement_file_name
         << endl;
    return false;
  }
  return true;
}

void sendMeasurementEvent(float measurement) {
  char number_buffer[21];
  dtostrf(measurement, -1, 7, number_buffer);
  events.send(number_buffer, "measurement", millis(), false);
}

void handleNewMeasurements() {
  if (!openMeasurementFileAppending()) {
    return;
  }
  // Write buffers
  for (uint32_t i = 0; i < measurements_buffer.available(); ++i) {
    float measurement = measurements_buffer.pop();

    if (measurement_file.write(reinterpret_cast<uint8_t *>(&measurement), 4) !=
        4) {
      cout << "Writing measurement to ssd failed" << endl;
    }
    sendMeasurementEvent(measurement);
  }
  measurement_file.close();
}

float previous_measurement = -4200;
size_t good_sync_messages = 0;
size_t bad_sync_messages = 0;

// Automatically syncing serial connection
bool getMeasurementFromArduino(float *value) {
  uint32_t t0 = millis();
  // Syncing
  if (arduino_serial.available() >= 4) {
    if (good_sync_messages < 10) {
      arduino_serial.read(reinterpret_cast<char *>(value), 4);
      if (fabs(*value - previous_measurement) < 50.f) { // sync criterion
        good_sync_messages++;
        bad_sync_messages = 0;
      } else {
        good_sync_messages = 0;
        bad_sync_messages++;
      }
      previous_measurement = *value;
      if (bad_sync_messages > 2) {
        while (!arduino_serial.available()) {
          yield();
        }
        // read one byte and see if it is now in sync
        arduino_serial.read();
        bad_sync_messages = 0;
      }
      cout << "Good sync messages: " << good_sync_messages << ", "
           << "bad sync messages: " << bad_sync_messages << endl;
      return false;
    } else {
      arduino_serial.read(reinterpret_cast<char *>(value), 4);
      return true;
    }
  }
  return false;
}

void checkArdinoForMeasurements() {
  while (arduino_serial.available() >= 4) {
    uint32_t receive_ts = millis();
    float measurement;
    if (!getMeasurementFromArduino(&measurement)) {
      return;
    }
    bool push_success = measurements_buffer.push(measurement);
    if (!push_success) {
      cout << "WARNING: measurement buffer ran full ... dropping measurements!"
           << endl;
    }
  }
}

void loop() {
  checkArdinoForMeasurements();
  if (measurements_buffer.available() > 0) {
    // Let clients know about the new measurements and write everything new to
    // the file
    handleNewMeasurements();

  }
}
