#include "HardwareSerial.h"
#include "core_esp8266_features.h"
#include <Arduino.h>
#include <AsyncJson.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <NTPClient.h>
#include <SdFat.h>
#include <SoftwareSerial.h>
#include <WiFiUdp.h>
#include <circular_queue/circular_queue.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream/ArduinoStream.h>
#include <memory>
#include <strings.h>

#include "csv_format.h"
#include "diagnostics.h"
#include "infrasound_frame.h"
#include "led_status.h"
#include "logger.h"
#include "measurement_file.h"
#include "static_sync.h"
#include "wifi_credentials.h"

#define SPI_SPEED SD_SCK_MHZ(4)
#define MYPORT_TX 5
#define MYPORT_RX 4

EspSoftwareSerial::UART esp_serial;

bool is_ssid_input_required;
bool is_password_input_required;
String ssid = "no_wifi";
String password = "";
AsyncWebServer server(80);
AsyncEventSource events("/measurement_events");

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");
const char *wifi_acces_file_path = "wifi_ssid_pw.txt";

// Networks parsed from wifi_acces_file_path, in priority order. Populated by
// load_wifi_credentials(), walked by initWifi().
infrasound::WifiCredential g_wifi_credentials[infrasound::WIFI_MAX_NETWORKS];
size_t g_wifi_credential_count = 0;

// Read buffer for the whole credentials file. Static (not on-stack) because
// the ESP8266 has very little spare stack.
constexpr size_t kWifiFileBufferSize = 2048;
static char g_wifi_file_buffer[kWifiFileBufferSize];

// SSD Chip Select pin
const int sd_chip_select = SS;
SdFs sd;

// Printing and reading from USB with cout and cin
ArduinoOutStream cout(Serial);
// input buffer for line
char cinBuf[128];
ArduinoInStream cin(Serial, cinBuf, sizeof(cinBuf));

// Some global variables to enable or disable features based on connected
// hardware
bool is_sd_card_available = false;
bool is_wifi_client = false;
uint64_t start_timestamp = 0;
volatile bool is_measurement_running = true;

// A single queue keeps the timestamp and value in lockstep structurally,
// rather than by the hand-maintained invariant the two parallel queues relied
// on ("NOTE: we assume that this buffer always has the same size!").
struct Sample {
  uint32_t t_rel_ms;
  float v;
};

circular_queue<Sample> sample_queue(64);

infrasound::Diagnostics g_diag;
infrasound::FrameDecoder g_decoder;

// Sensor-clock origin for the current part file.
uint32_t sensor_t0 = 0;
bool sensor_t0_valid = false;
uint32_t last_accepted_ms = 0;  // service-module millis(), for reseed timing

// Sensor-clock origin. t_rel_ms == 0 means "the instant of the first accepted
// frame", which is NOT the instant the service module booted — boot, SD init,
// WiFi association and NTP all happen first. service_millis_at_t0 records the
// local clock at that instant so the logger can turn t_rel_ms into wall-clock
// time correctly; using start_timestamp alone would shift every absolute
// timestamp early by however long startup took.
uint32_t service_millis_at_t0 = 0;

String measurement_file_name;

IPAddress local_IP(192, 168, 4, 1);
IPAddress gateway(192, 168, 4, 1);
IPAddress subnet(255, 255, 255, 0);

void ledTick(uint32_t now_ms) {
  infrasound::LedState state;
  if (!is_sd_card_available) {
    state = infrasound::LedState::SdFailure;
  } else if (is_measurement_running && !loggerIsLogging()) {
    // "We are supposed to be logging but are not" — a real failure. A
    // deliberate stop clears is_measurement_running too, so analysis mode
    // keeps the heartbeat, per the contract in led_status.h.
    state = infrasound::LedState::NotLogging;
  } else if (start_timestamp == 0) {
    state = infrasound::LedState::NoTime;
  } else {
    state = infrasound::LedState::Ok;
  }
  // LED_BUILTIN on the D1 Mini is active LOW.
  digitalWrite(LED_BUILTIN, infrasound::ledOn(state, now_ms) ? LOW : HIGH);
}

// forward declarations
bool initWifi();
void initWebserver();
void pollSensorISR();

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

// Reads wifi_acces_file_path in full and parses it into g_wifi_credentials.
// Returns true when at least one credential was parsed. Does not attempt any
// connection — see initWifi() for that.
bool load_wifi_credentials() {
  cout << "Loading WiFi credentials from SDCard" << endl;
  g_wifi_credential_count = 0;

  FsFile wifi_credential_file;
  if (!wifi_credential_file.open(wifi_acces_file_path, O_RDONLY)) {
    cout << "WiFi credential file does not exist: " << wifi_acces_file_path
         << endl;
    return false;
  }

  const int n = wifi_credential_file.read(g_wifi_file_buffer,
                                           sizeof(g_wifi_file_buffer) - 1);
  wifi_credential_file.close();
  if (n < 0) {
    cout << "Failed to read WiFi credential file" << endl;
    return false;
  }
  g_wifi_file_buffer[n] = '\0';

  infrasound::WifiParseReport report;
  g_wifi_credential_count = infrasound::wifiParseCredentials(
      g_wifi_file_buffer, g_wifi_credentials, infrasound::WIFI_MAX_NETWORKS,
      &report);

  if (report.legacy) {
    cout << "WiFi credential file is in the legacy single-network format"
         << endl;
  }
  cout << "Parsed " << g_wifi_credential_count
       << " WiFi credential(s) from " << wifi_acces_file_path << endl;
  if (report.malformed > 0) {
    cout << "WARNING: " << report.malformed
         << " malformed line(s) in WiFi credential file (no space "
            "separator) were skipped"
         << endl;
  }
  if (report.over_capacity > 0) {
    cout << "WARNING: " << report.over_capacity
         << " WiFi credential(s) dropped, more than "
         << infrasound::WIFI_MAX_NETWORKS << " configured" << endl;
  }

  return g_wifi_credential_count > 0;
}

// Appends one "SSID password\n" line for the currently-typed-in ssid/password
// globals. Appends rather than overwrites so adding a network on site does
// not discard the ones that already work elsewhere.
bool write_wifi_credentials() {
  cout << "Appending WiFi credentials for ssid: " << ssid << endl;

  // Check whether the file already ends in a newline; appending directly to
  // a file that doesn't would join our new line onto the previous one.
  bool needs_leading_newline = false;
  {
    FsFile existing;
    if (existing.open(wifi_acces_file_path, O_RDONLY)) {
      char probe[64];
      int read_count;
      bool have_content = false;
      char last_byte = '\n';
      while ((read_count = existing.read(probe, sizeof(probe))) > 0) {
        have_content = true;
        last_byte = probe[read_count - 1];
      }
      existing.close();
      needs_leading_newline = have_content && last_byte != '\n';
    }
  }

  FsFile wifi_credential_file;
  if (!wifi_credential_file.open(wifi_acces_file_path,
                                  O_WRONLY | O_CREAT | O_APPEND | O_AT_END)) {
    return false;
  }
  if (needs_leading_newline) {
    wifi_credential_file.print("\n");
  }
  wifi_credential_file.print(ssid + " " + password + "\n");
  wifi_credential_file.close();
  return true;
}

void onNotFound(AsyncWebServerRequest *request) {
  cout << "A unknown request was sent: " << request->url() << endl;
  request->send(404, "text/plain", "Not found");
}

void onGetDownloads(AsyncWebServerRequest *request) {
  AsyncJsonResponse *response = new AsyncJsonResponse();
  response->addHeader("InfrasoundSensor", "ESP Infrasound sensor webserver");
  JsonObject root = response->getRoot();
  JsonArray file_list = root["files"].to<JsonArray>();

  FsFile directory;
  if (!directory.open("/measurements", O_RDONLY)) {
    response->setLength();
    request->send(response);
    return;
  }
  directory.rewind();

  FsFile file;
  char file_name[64];
  while (file.openNext(&directory, O_RDONLY)) {
    if (file.isHidden() || file.isDir()) {
      file.close();
      continue;
    }
    file.getName(file_name, sizeof(file_name));
    // isHidden() reflects the FAT hidden attribute, not a leading dot, so
    // /measurements/.run (the run counter written by
    // readAndIncrementRunCounter()) would otherwise show up here as a
    // one-record "legacy" file the user can click Analyse on.
    if (file_name[0] == '.') {
      file.close();
      continue;
    }

    uint8_t header[infrasound::MF_HEADER_SIZE];
    const int read = file.read(header, sizeof(header));
    infrasound::MeasurementFileHeader parsed{};
    const bool has_header =
        read == static_cast<int>(sizeof(header)) &&
        infrasound::mfParseHeader(header, &parsed);

    JsonObject entry = file_list.add<JsonObject>();
    entry["name"] = file_name;
    entry["bytes"] = file.fileSize();
    entry["records"] =
        infrasound::mfRecordCount(file.fileSize(), !has_header);
    entry["epoch0_ms"] = has_header ? parsed.epoch0_ms : 0;
    entry["legacy"] = !has_header;

    file.close();
  }
  directory.close();

  response->setLength();
  request->send(response);
}

namespace {

// Opens a measurement file once per request. The FsFile is owned by a
// shared_ptr captured in the chunk lambda, so it stays open for the life of
// the response and closes when the response is destroyed. The previous code
// opened and closed the file on every chunk.
struct FileContext {
  FsFile file;
  bool legacy = false;
  uint64_t epoch0_ms = 0;
  uint32_t total_records = 0;
  ~FileContext() { if (file.isOpen()) file.close(); }
};

std::shared_ptr<FileContext> openMeasurement(const String& name) {
  auto ctx = std::make_shared<FileContext>();
  const String path = "/measurements/" + name;
  if (!ctx->file.open(path.c_str(), O_RDONLY)) {
    return nullptr;
  }

  uint8_t header[infrasound::MF_HEADER_SIZE];
  const int read = ctx->file.read(header, sizeof(header));
  infrasound::MeasurementFileHeader parsed{};
  if (read == static_cast<int>(sizeof(header)) &&
      infrasound::mfParseHeader(header, &parsed)) {
    ctx->legacy = false;
    ctx->epoch0_ms = parsed.epoch0_ms;
  } else {
    ctx->legacy = true;
    ctx->epoch0_ms = 0;
  }
  ctx->total_records =
      infrasound::mfRecordCount(ctx->file.fileSize(), ctx->legacy);
  return ctx;
}

uint32_t paramU32(AsyncWebServerRequest* request, const char* name,
                  uint32_t fallback) {
  if (!request->hasParam(name)) return fallback;
  return static_cast<uint32_t>(
      strtoul(request->getParam(name)->value().c_str(), nullptr, 10));
}

// Clamps [*from, *from + *count) to the file's record range. *from must
// already be <= ctx.total_records when called, so ctx.total_records - *from
// cannot underflow. Compares by subtraction rather than *from + *count >
// ctx.total_records: both are uint32_t, so that sum wraps modulo 2^32 -- a
// query string like count=4294967246 (strtoul happily returns it) can make
// from + count wrap back below total_records, defeating the clamp entirely
// and leaving count near 4.29 billion.
void clampRange(const FileContext& ctx, uint32_t* from, uint32_t* count) {
  if (*from > ctx.total_records) *from = ctx.total_records;
  if (*count > ctx.total_records - *from) *count = ctx.total_records - *from;
}

// Reads record `index` into t and v, handling both formats.
bool readRecord(FileContext& ctx, uint32_t index, uint32_t* t, float* v) {
  if (ctx.legacy) {
    if (!ctx.file.seek(static_cast<uint64_t>(index) * 4)) return false;
    if (ctx.file.read(v, 4) < 4) return false;
    *t = infrasound::mfLegacyTimestamp(index);
    return true;
  }
  const uint64_t at = infrasound::MF_HEADER_SIZE +
                      static_cast<uint64_t>(index) * infrasound::MF_RECORD_SIZE;
  if (!ctx.file.seek(at)) return false;
  uint8_t rec[infrasound::MF_RECORD_SIZE];
  if (ctx.file.read(rec, sizeof(rec)) < static_cast<int>(sizeof(rec))) {
    return false;
  }
  infrasound::mfParseRecord(rec, t, v);
  return true;
}

}  // namespace

// Binary: 32-byte header followed by `count` 8-byte records from `from`.
// Serves the analysis page and enables progressive rendering and zoom.
void onRaw(AsyncWebServerRequest *request) {
  if (!request->hasParam("file")) {
    request->send(400, "text/plain", "missing file parameter");
    return;
  }
  const String file_name = request->getParam("file")->value();
  auto ctx = openMeasurement(file_name);
  if (!ctx) {
    request->send(404, "text/plain", "no such measurement file");
    return;
  }

  uint32_t from = paramU32(request, "from", 0);
  if (from > ctx->total_records) from = ctx->total_records;
  uint32_t count = paramU32(request, "count", ctx->total_records - from);
  clampRange(*ctx, &from, &count);

  const uint64_t body_bytes =
      infrasound::MF_HEADER_SIZE +
      static_cast<uint64_t>(count) * infrasound::MF_RECORD_SIZE;

  AsyncWebServerResponse *response = request->beginChunkedResponse(
      "application/octet-stream",
      [ctx, from, count, body_bytes](uint8_t *buffer, size_t maxLen,
                                     size_t index) -> size_t {
        if (index >= body_bytes) return 0;

        // maxLen == 0 cannot be turned into forward progress: there is no
        // buffer to write into. AsyncWebServer's chunked-response contract
        // treats any 0 return as end-of-response -- it has no distinct
        // "not ready yet, but not finished" signal -- so this one case
        // cannot be fixed from inside the callback. Left explicit rather
        // than falling through so the limitation is visible. Believed
        // unreachable in practice: the library only invokes this callback
        // with a real, positive-capacity TCP write buffer.
        if (maxLen == 0) return 0;

        size_t written = 0;

        if (index < infrasound::MF_HEADER_SIZE) {
          infrasound::MeasurementFileHeader h{};
          h.version   = infrasound::MF_VERSION;
          h.rec_size  = infrasound::MF_RECORD_SIZE;
          h.rate_hz   = infrasound::MF_RATE_HZ;
          h.epoch0_ms = ctx->epoch0_ms;
          uint8_t header[infrasound::MF_HEADER_SIZE];
          infrasound::mfPackHeader(h, header);
          const size_t remaining = infrasound::MF_HEADER_SIZE - index;
          written = remaining < maxLen ? remaining : maxLen;
          memcpy(buffer, header + index, written);
          return written;
        }

        // Position-based, not record-based: resume mid-record if the
        // previous chunk ended inside one. A record-aligned loop like
        // `while (written + MF_RECORD_SIZE <= maxLen)` returns 0 whenever
        // maxLen < MF_RECORD_SIZE, and AsyncWebServer reads a 0 return as
        // end-of-response -- silently truncating the download after
        // whatever was already sent. Re-packing the current record each
        // iteration is cheap and only the first iteration can be partial.
        const uint64_t body_off = index - infrasound::MF_HEADER_SIZE;
        uint32_t rec_index =
            from + static_cast<uint32_t>(body_off / infrasound::MF_RECORD_SIZE);
        size_t rec_off =
            static_cast<size_t>(body_off % infrasound::MF_RECORD_SIZE);

        while (written < maxLen && (rec_index - from) < count) {
          uint32_t t = 0;
          float v = 0.0f;
          if (!readRecord(*ctx, rec_index, &t, &v)) break;
          uint8_t rec[infrasound::MF_RECORD_SIZE];
          infrasound::mfPackRecord(t, v, rec);

          const size_t avail = infrasound::MF_RECORD_SIZE - rec_off;
          const size_t take = avail < (maxLen - written) ? avail : (maxLen - written);
          memcpy(buffer + written, rec + rec_off, take);
          written += take;
          rec_off += take;
          if (rec_off == infrasound::MF_RECORD_SIZE) {
            rec_off = 0;
            ++rec_index;
          }
        }
        return written;
      });

  response->addHeader("InfrasoundSensor", "ESP Infrasound sensor webserver");
  request->send(response);
}

// CSV for Excel/Python. Fixed-width lines are what make the chunk callback
// restartable: record N always sits at output offset 20 + N*24.
void onDownload(AsyncWebServerRequest *request) {
  if (!request->hasParam("file")) {
    request->send(400, "text/plain", "missing file parameter");
    return;
  }
  const String file_name = request->getParam("file")->value();
  auto ctx = openMeasurement(file_name);
  if (!ctx) {
    request->send(404, "text/plain", "no such measurement file");
    return;
  }

  uint32_t from = paramU32(request, "from", 0);
  if (from > ctx->total_records) from = ctx->total_records;
  uint32_t count = paramU32(request, "count", ctx->total_records - from);
  clampRange(*ctx, &from, &count);

  const uint64_t body_bytes =
      infrasound::CSV_HEADER_LEN +
      static_cast<uint64_t>(count) * infrasound::CSV_LINE_LEN;

  AsyncWebServerResponse *response = request->beginChunkedResponse(
      "text/csv",
      [ctx, from, count, body_bytes](uint8_t *buffer, size_t maxLen,
                                     size_t index) -> size_t {
        if (index >= body_bytes) return 0;

        // maxLen == 0 cannot be turned into forward progress: there is no
        // buffer to write into. AsyncWebServer's chunked-response contract
        // treats any 0 return as end-of-response -- it has no distinct
        // "not ready yet, but not finished" signal -- so this one case
        // cannot be fixed from inside the callback. Left explicit rather
        // than falling through so the limitation is visible. Believed
        // unreachable in practice: the library only invokes this callback
        // with a real, positive-capacity TCP write buffer.
        if (maxLen == 0) return 0;

        size_t written = 0;
        char line[infrasound::CSV_LINE_LEN + 1];

        if (index < infrasound::CSV_HEADER_LEN) {
          char header[infrasound::CSV_HEADER_LEN + 1];
          infrasound::csvHeader(header);
          const size_t remaining = infrasound::CSV_HEADER_LEN - index;
          written = remaining < maxLen ? remaining : maxLen;
          memcpy(buffer, header + index, written);
          return written;
        }

        // Position-based, not record-based: resume mid-line if the previous
        // chunk ended inside one. Returning 0 here would be read as
        // end-of-response and silently truncate the download, so the
        // callback must always make progress when there is at least one
        // byte of space -- a record-aligned loop (`written + CSV_LINE_LEN <=
        // maxLen`) returns 0 whenever maxLen < CSV_LINE_LEN, which
        // AsyncWebServer cannot distinguish from EOF.
        const uint64_t body_off = index - infrasound::CSV_HEADER_LEN;
        uint32_t rec_index =
            from + static_cast<uint32_t>(body_off / infrasound::CSV_LINE_LEN);
        size_t line_off =
            static_cast<size_t>(body_off % infrasound::CSV_LINE_LEN);

        while (written < maxLen && (rec_index - from) < count) {
          uint32_t t = 0;
          float v = 0.0f;
          if (!readRecord(*ctx, rec_index, &t, &v)) break;
          infrasound::csvLine(ctx->epoch0_ms + t, v, line);

          const size_t avail = infrasound::CSV_LINE_LEN - line_off;
          const size_t take = avail < (maxLen - written) ? avail : (maxLen - written);
          memcpy(buffer + written, line + line_off, take);
          written += take;
          line_off += take;
          if (line_off == infrasound::CSV_LINE_LEN) {
            line_off = 0;
            ++rec_index;
          }
        }
        return written;
      });

  String disposition = "attachment; filename=\"" + file_name + ".csv\"";
  response->addHeader("Content-Disposition", disposition);
  // No Content-Length: this is a chunked response, and setting both is what
  // helped break the previous implementation.
  request->send(response);
}

void onConnect(AsyncEventSourceClient *client) {
  if (client->lastId()) {
    cout << "Client " << client->lastId() << " has reconnected" << endl;
  } else {
    cout << "A client has connected to the measurement event" << endl;
  }
}

void onStartTimestamp(AsyncWebServerRequest *request) {
  AsyncResponseStream *response = request->beginResponseStream("text/plain");
  // See loggerEpoch0Ms() in logger.h for why this is centralised.
  const uint64_t epoch0 = loggerEpoch0Ms();
  response->printf("%llu", epoch0);
  request->send(response);
}

void onStartMeasurement(AsyncWebServerRequest *request) {
  if (!is_measurement_running) {
    cout << "Starting measurement" << endl;
    loggerStartNewPart();
    cout << "Adding handler for /measurement_event" << endl;
    events.onConnect(onConnect);
    server.addHandler(&events);
  } else {
    cout << "Measurements are already running" << endl;
  }
  is_measurement_running = true;
  request->send(200);
}

void onStopMeasurement(AsyncWebServerRequest *request) {
  if (is_measurement_running) {
    cout << "Stopping measurement" << endl;
    cout << "Removing handler for /measurement_event" << endl;
    events.onConnect(onConnect);
    server.removeHandler(&events);
  } else {
    cout << "Measurements are already stopped" << endl;
  }
  is_measurement_running = false;
  loggerStop();
  request->send(200);
}

void initWebserver() {

  // AsyncFileResponse's MIME table has no .wasm case and serveStatic offers no
  // per-extension override, so WebAssembly would otherwise be served as
  // text/plain. Emscripten falls back to ArrayBuffer instantiation, but that is
  // slower and depends on a catch path -- serve the correct type explicitly.
  server.on("/pffft/pffft.wasm", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/static/pffft/pffft.wasm", "application/wasm");
  });

  cout << "serving /downloads" << endl;
  server.on("/downloads", HTTP_GET, onGetDownloads);
  cout << "serving /download" << endl;
  server.on("/download", HTTP_GET, onDownload);
  cout << "serving /raw" << endl;
  server.on("/raw", HTTP_GET, onRaw);
  cout << "serving /start_timestamp" << endl;
  server.on("/start_timestamp", HTTP_GET, onStartTimestamp);
  cout << "serving /start_measurements" << endl;
  server.on("/start_measurements", HTTP_GET, onStartMeasurement);
  cout << "serving /stop_measurements" << endl;
  server.on("/stop_measurements", HTTP_GET, onStopMeasurement);

  // Serve everything under LittleFS /static/ directly, replacing the old
  // per-request open/seek/read/close handler (onStaticFile) that walked the
  // SD card on every ~1.4 KB chunk. setDefaultFile("index.html") makes "/"
  // serve the index directly (200), so the old onIndex redirect handler is
  // gone too -- no reason to pay a redirect round trip when serveStatic can
  // just answer the request.
  //
  // Registered last, after every explicit server.on(...) route above
  // (including the pffft.wasm override), ON PURPOSE: ESPAsyncWebServer
  // resolves requests in strict registration order, trying each handler's
  // canHandle() in turn, so an explicit route registered after serveStatic
  // would still win -- but a static handler registered after the explicit
  // routes can never shadow them. Today AsyncStaticWebHandler::canHandle()
  // only matches when the requested file actually exists under /static/, so
  // a same-named endpoint like /downloads would already fall through even
  // with serveStatic registered first; registering it last makes that
  // safety structural rather than a fact about the library's internals that
  // this codebase cannot compile against to verify.
  //
  // Two registrations, most specific first: the vendored third-party blobs
  // under /highcharts/, /bootstrap/ and /pffft/ are versioned-in-place and
  // never change without a content update, so they get a one-year immutable
  // cache. Everything else under /static/ (index.html, analyse.html, the
  // *_client.js files, favicon.ico, ...) is source this project edits
  // in-place per the README's SD-card update workflow, so it must not be
  // cached long-term -- a browser that already fetched measurement_parsing.js
  // under an immutable header would keep using the stale copy for a year
  // after a reboot picks up a newer file format. Both prefix handlers are
  // registered before the "/" catch-all, so AsyncStaticWebHandler's
  // registration-order matching gives the prefix handler first refusal on
  // its own paths; "/" only ever sees requests the prefix handlers declined.
  cout << "Serving vendored static assets from LittleFS (long-lived cache)"
       << endl;
  server.serveStatic("/highcharts/", LittleFS, "/static/highcharts/")
      .setCacheControl("max-age=31536000, immutable");
  server.serveStatic("/bootstrap/", LittleFS, "/static/bootstrap/")
      .setCacheControl("max-age=31536000, immutable");
  server.serveStatic("/pffft/", LittleFS, "/static/pffft/")
      .setCacheControl("max-age=31536000, immutable");

  // gzip: AsyncStaticWebHandler (lacamera/ESPAsyncWebServer 3.1.0) already
  // implements the adaptive gzip heuristic via _gzipFirst/_gzipStats in
  // WebHandlers.cpp -- if "<path>.gz" exists next to "<path>" under
  // /static/, it is served automatically with Content-Encoding: gzip. This
  // needs no device code; it is purely a PC-side packaging choice when
  // populating /www/static on the SD card before the Task 17 sync. See
  // README (Task 23) for the packaging note.
  cout << "Serving static files from LittleFS" << endl;
  server.serveStatic("/", LittleFS, "/static/")
      .setDefaultFile("index.html")
      .setCacheControl("no-cache");

  server.onNotFound(onNotFound);

  cout << "Setting up handler for /measurement_event" << endl;
  events.onConnect(onConnect);
  server.addHandler(&events);

  server.begin();
}

// Connects to the first configured network that is both reachable (seen in a
// scan) and actually accepts the stored password. Credentials are tried in
// file order (== priority order). A single scan is used to decide which
// configured SSIDs are even worth a WiFi.begin() attempt, because
// waitForConnectResult()'s default timeout is 60 s per attempt and a list of
// networks makes that add up fast.
bool initWifi() {
  if (!load_wifi_credentials()) {
    cout << "No usable WiFi credentials found; skipping WiFi" << endl;
    is_wifi_client = false;
    return false;
  }

  WiFi.mode(WIFI_STA);
  const int16_t scan_result = WiFi.scanNetworks();
  const int16_t networks_found = scan_result > 0 ? scan_result : 0;
  cout << "WiFi scan found " << networks_found << " network(s) in range"
       << endl;

  // Decide up front which configured networks are even in range, so a
  // failure to connect to any of them can be reported clearly.
  bool present[infrasound::WIFI_MAX_NETWORKS] = {};
  size_t present_count = 0;
  for (size_t i = 0; i < g_wifi_credential_count; ++i) {
    for (int16_t j = 0; j < networks_found; ++j) {
      if (WiFi.SSID(j) == g_wifi_credentials[i].ssid) {
        present[i] = true;
        ++present_count;
        break;
      }
    }
  }
  cout << present_count << " of " << g_wifi_credential_count
       << " configured network(s) are in range" << endl;

  bool connected = false;
  if (present_count == 0) {
    cout << "None of the configured SSIDs were seen in the scan; not "
            "attempting to connect"
         << endl;
  } else {
    for (size_t i = 0; i < g_wifi_credential_count && !connected; ++i) {
      if (!present[i]) continue;
      const infrasound::WifiCredential &cred = g_wifi_credentials[i];
      cout << "Attempting to connect to: " << cred.ssid << endl;
      WiFi.begin(cred.ssid, cred.password);
      if (WiFi.waitForConnectResult(15000) == WL_CONNECTED) {
        cout << "Connected to: " << cred.ssid << endl;
        ssid = cred.ssid;
        password = cred.password;
        connected = true;
      } else {
        cout << "Failed to connect to: " << cred.ssid << endl;
      }
    }
  }

  // Free the scan results on every path, not just success.
  WiFi.scanDelete();

  if (!connected) {
    cout << "Failed to connect to any configured WiFi network" << endl;
    is_wifi_client = false;
    return false;
  }

  cout << "IP Address: " << WiFi.localIP().toString() << endl;
  is_wifi_client = true;
  return true;
}

// Returns true when a real time was obtained. The previous implementation
// ignored timeClient.update(), so on failure getEpochTime() returned ~0 and
// `epochTime * 1000 - millis()` underflowed the uint64 into an enormous value,
// producing a garbage filename.
bool tryUpdateTimestamp() {
  if (!timeClient.update()) {
    ++g_diag.ntp_failures;
    return false;
  }
  const time_t epochTime = timeClient.getEpochTime();
  if (epochTime <= 0) {
    ++g_diag.ntp_failures;
    return false;
  }
  const uint64_t epoch_ms = static_cast<uint64_t>(epochTime) * 1000ull;
  const uint64_t uptime_ms = millis();
  if (epoch_ms <= uptime_ms) {
    ++g_diag.ntp_failures;
    return false;
  }
  start_timestamp = epoch_ms - uptime_ms;
  cout << "Starttimestamp is " << start_timestamp << " ms" << endl;
  return true;
}

void initTimestamp() {
  cout << "Initializing start-timestamp" << endl;
  timeClient.begin();
  for (int attempt = 0; attempt < 3; ++attempt) {
    if (tryUpdateTimestamp()) return;
    delay(500);
  }
  cout << "NTP unavailable; recording with relative timestamps" << endl;
}

String millisecondsToTimeString(uint64_t milliseconds) {
  // Convert milliseconds to seconds
  uint64_t seconds = milliseconds / 1000;

  // Get time components
  uint16_t year, month, day, hour, minute, second, millisecond;
  time_t time_t_seconds = seconds;
  struct tm *timeinfo = localtime(&time_t_seconds);

  year = timeinfo->tm_year + 1900;
  month = timeinfo->tm_mon + 1;
  day = timeinfo->tm_mday;
  hour = timeinfo->tm_hour;
  minute = timeinfo->tm_min;
  second = timeinfo->tm_sec;

  millisecond = milliseconds % 1000;

  // Format the string
  char buffer[32];
  sprintf(buffer, "%04d_%02d_%02d - %02d-%02d-%02d_%03d", year, month, day,
          hour, minute, second, millisecond);

  return String(buffer);
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

  // GPIO2 is a boot-strap pin that must be HIGH at reset, so it is only driven
  // as an output here, after boot.
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);  // solid on while the SD card is being found

  // Initialize SD-Card
  while (!initSdCard()) {
    cout << "Failed to initialize SD-Card..." << endl;
    delay(1000);
    cout << "Trying again" << endl;
  }

  // Copy /www/static from the SD card into LittleFS so the HTTP server never
  // touches the SD card to serve the web UI. Must run before initWebserver(),
  // which is what actually reads the synced assets (Task 18). Runs
  // unconditionally, not just when WiFi is up, so the SD card becomes
  // optional for the web UI after the first successful boot.
  staticSyncRun();

  const uint32_t run_number = readAndIncrementRunCounter();
  cout << "Measurement run " << run_number << endl;
  if (!loggerBegin(run_number)) {
    cout << "ERROR: could not start logging - measurements will not be saved"
         << endl;
  }

  // init wifi
  is_ssid_input_required = !initWifi();

  // clear Serial in
  cout << "Clearing Serial in" << endl;
  String dummy = Serial.readString();

  if (is_ssid_input_required) {
    is_password_input_required = false;
    cout
        << "Please type in SSID (name) of the WiFi network to connect to.\nYou "
           "have 10 seconds before sensor starts in offline mode."
        << endl;
    unsigned long start_millis = millis();
    while (millis() - start_millis < 10 * 1000) {
      ssid = Serial.readStringUntil('\n');
      // readStringUntil('\n') keeps everything before the newline, including
      // the '\r' a Windows serial monitor sends for Enter. Without trim(),
      // that '\r' becomes part of the SSID and both the connection attempt
      // and the saved credential silently fail. A whitespace-only line must
      // become empty too, so this happens before the isEmpty() check.
      ssid.trim();
      if (!ssid.isEmpty()) {
        is_ssid_input_required = false;
        is_password_input_required = true;
        break;
      }
    }
    if (is_password_input_required) {
      cout << "Please type in the password for " << ssid
           << ".\nYou have 1 minute";
      while (millis() - start_millis < 60 * 1000) {
        password = Serial.readStringUntil('\n');
        // Same '\r'-from-Enter problem as the SSID above.
        password.trim();
        if (!password.isEmpty()) {
          is_ssid_input_required = false;
          is_password_input_required = false;
          break;
        }
      }
      if (!is_password_input_required) {
        // The credentials file splits each line at the LAST space, so a
        // password containing a space cannot round-trip: it would be split
        // between the "SSID" and the password on the next boot, silently
        // wrong. Refuse to save it rather than write a file that won't
        // parse back the way the user typed it.
        if (password.indexOf(' ') != -1) {
          cout << "Password contains a space, which this file format "
                  "cannot store (lines split on the last space). Not "
                  "saving. Restarting so you can try again."
               << endl;
          ESP.restart();
        }
        cout << "Got the password. Trying to connect to the given WiFi."
             << endl;
        if (write_wifi_credentials() && initWifi()) {
          cout << "Successfully connected to " << ssid << endl;
          cout << "This WiFi is now saved and will automatically be connected "
                  "from now on."
               << endl;
        } else {
          cout << "Failed to connect. Restarting to give you another chance."
               << endl;
          ESP.restart();
        }
      } else {
        cout << "Time has passed... resuming without wifi";
      }
    }
  }

  is_measurement_running = true;

  // init time
  if (is_wifi_client) {
    initTimestamp();

    // Setup Webserver
    initWebserver();
  }

  // Connection to Arduino serial using software serial.
  //
  // Opened last, immediately before loop() starts draining it. initTimestamp()
  // blocks for several seconds (up to three NTP attempts, 500 ms apart, plus
  // timeClient.update()) and initWebserver() takes its own time; opening
  // esp_serial before them left the receive buffer filling with nobody
  // reading it. That overflow corrupted a frame, which the decoder then
  // seeded its time reference from, desynchronising it for a full
  // MAX_PLAUSIBLE_GAP_MS window -- exactly the "ovf=1 crc=N frm=50" pinned
  // from startup seen in the field before armReseed() recovered it.
  cout << "Connecting to Sensor Board" << endl;
  // 256-byte byte buffer and 2048-entry ISR edge buffer: ~8.25 KB of RAM for
  // roughly 370 ms of stall tolerance, up from the ~128 ms the library
  // defaults gave. Insurance on top of removing the stalls, not a substitute.
  esp_serial.begin(infrasound::LINK_BAUD, SWSERIAL_8N1, MYPORT_RX, MYPORT_TX,
                    false, 256, 2048);
  if (!esp_serial) {
    cout << "Invalid EspSoftwareSerial pin configuration, check config!";
    // don't continue with broken configuration
    wait_forever();
  }
  cout << "Connection to Sensor Board established" << endl;
}

namespace {
constexpr size_t   SSE_BATCH_MAX     = 10;
constexpr uint32_t SSE_MAX_AGE_MS    = 250;
constexpr uint32_t SSE_GAP_MS        = 30;  // 1.5 sample periods

float    g_sse_values[SSE_BATCH_MAX];
size_t   g_sse_count   = 0;
uint32_t g_sse_first_t = 0;
uint32_t g_sse_last_t  = 0;
uint32_t g_sse_started_ms = 0;
}  // namespace

void sseFlush() {
  if (g_sse_count == 0) return;

  // t0;v0,v1,... — samples in one message are contiguous by construction
  // because a gap forces a flush, so one timestamp describes the whole batch.
  char message[192];
  int n = snprintf(message, sizeof(message), "%lu;",
                   static_cast<unsigned long>(g_sse_first_t));
  for (size_t i = 0; i < g_sse_count && n > 0 && n < (int)sizeof(message); ++i) {
    n += snprintf(message + n, sizeof(message) - n, "%s%.5f",
                  i == 0 ? "" : ",", static_cast<double>(g_sse_values[i]));
  }
  events.send(message, "measurement", millis(), false);
  g_sse_count = 0;
}

void sseAppend(uint32_t t_rel_ms, float v) {
  // Skip all formatting when nobody is watching. The previous code formatted
  // and sent 50 messages a second regardless of whether a client existed.
  if (!is_measurement_running || !is_wifi_client || events.count() == 0) {
    g_sse_count = 0;
    return;
  }

  if (g_sse_count > 0 &&
      static_cast<uint32_t>(t_rel_ms - g_sse_last_t) > SSE_GAP_MS) {
    sseFlush();  // a real discontinuity ends the batch
  }

  if (g_sse_count == 0) {
    g_sse_first_t = t_rel_ms;
    g_sse_started_ms = millis();
  }
  g_sse_values[g_sse_count++] = v;
  g_sse_last_t = t_rel_ms;

  if (g_sse_count >= SSE_BATCH_MAX) {
    sseFlush();
  }
}

void sseTick(uint32_t now_ms) {
  if (g_sse_count > 0 && (now_ms - g_sse_started_ms) >= SSE_MAX_AGE_MS) {
    sseFlush();
  }
}

void handleNewMeasurements() {
  const size_t pending = sample_queue.available();
  for (size_t i = 0; i < pending; ++i) {
    const Sample sample = sample_queue.pop();
    loggerAppend(sample.t_rel_ms, sample.v);   // Task 12
    sseAppend(sample.t_rel_ms, sample.v);      // Task 19
  }
}

void checkSensorForMeasurements() {
  if (esp_serial.overflow()) {
    ++g_diag.serial_overflows;
  }

  // Re-arm the decoder after a silence longer than the plausibility window, so
  // a sensor reboot or a long stall can resynchronise. Never armed as a
  // fallback for a single frame that failed the check.
  const uint32_t now = millis();
  if (last_accepted_ms != 0 &&
      (now - last_accepted_ms) > infrasound::MAX_PLAUSIBLE_GAP_MS) {
    g_decoder.armReseed();
  }

  while (esp_serial.available() > 0) {
    char byte;
    if (esp_serial.read(&byte, 1) < 1) {
      break;
    }

    const infrasound::DecodedFrame frame =
        g_decoder.feed(static_cast<uint8_t>(byte));
    if (!frame.ok) {
      continue;  // a rejected byte advances the window by one; never bail out
    }

    last_accepted_ms = now;

    if (!sensor_t0_valid) {
      sensor_t0 = frame.t_ms;
      service_millis_at_t0 = now;
      sensor_t0_valid = true;
    } else if (frame.t_ms < sensor_t0) {
      // The sensor clock ran backwards: the sensor board rebooted. Close this
      // part and rebase, so the file's timestamps stay monotonic. Rebase
      // BEFORE opening the new part: openNewPart() writes epoch0_ms from
      // service_millis_at_t0 immediately, so moving the origin afterwards
      // would stamp the new part's header with the previous origin.
      ++g_diag.sensor_reboots;
      sensor_t0 = frame.t_ms;
      service_millis_at_t0 = now;
      loggerStartNewPart();
    }

    Sample sample;
    sample.t_rel_ms = static_cast<uint32_t>(frame.t_ms - sensor_t0);
    sample.v = frame.value;

    if (!sample_queue.push(sample)) {
      ++g_diag.queue_full_drops;
    }
  }

  g_diag.frames_crc_rejected = g_decoder.crcRejects();
  g_diag.frames_framing_rejected = g_decoder.framingRejects();
}

void diagnosticsTick(uint32_t now_ms) {
  static uint32_t last_report = 0;
  // Separate primed flag rather than treating 0 as "unset": 0 is also a
  // legitimate (if near-fatal) heap reading, and conflating them would let a
  // later, higher reading overwrite the true minimum.
  static bool heap_primed = false;
  const uint32_t free_heap = ESP.getFreeHeap();
  if (!heap_primed || free_heap < g_diag.min_free_heap) {
    g_diag.min_free_heap = free_heap;
    heap_primed = true;
  }

  if (now_ms - last_report < 60000) return;
  last_report = now_ms;

  char line[256];
  infrasound::formatDiagnostics(g_diag, line, sizeof(line));
  cout << line << endl;

  // max_loop_interval_ms is a per-window observation, so reset it; the loss
  // counters are cumulative and must not be reset.
  g_diag.max_loop_interval_ms = 0;
}

void loop() {
  static uint32_t last_loop_ms = 0;
  const uint32_t now = millis();
  if (last_loop_ms != 0) {
    const uint32_t interval = now - last_loop_ms;
    if (interval > g_diag.max_loop_interval_ms) {
      g_diag.max_loop_interval_ms = interval;
    }
  }
  last_loop_ms = now;

  if (is_measurement_running) {
    checkSensorForMeasurements();
    if (sample_queue.available() > 0) {
      handleNewMeasurements();
    }
    loggerTick(now);
  }

  ledTick(now);
  diagnosticsTick(now);
  sseTick(now);

  // NTP may become available after boot if WiFi reconnects. Retry every 60 s
  // while the time is still unset; later files then get a real epoch0_ms.
  // The `start_timestamp == 0` guard also makes this fire at most once: a
  // successful tryUpdateTimestamp() sets start_timestamp, which is what turns
  // this whole branch off on the next loop() iteration.
  static uint32_t last_ntp_try = 0;
  if (is_wifi_client && start_timestamp == 0 && (now - last_ntp_try) > 60000) {
    last_ntp_try = now;
    if (tryUpdateTimestamp()) {
      // The clock just became known for the first time after logging had
      // already started on relative-only timestamps (the unbekannt file).
      // Close that part and open a new one so the rest of the recording gets
      // a correct filename and epoch0_ms, rather than 24 h of wrong ones.
      // If no sample has arrived yet, loggerStartNewPart() is a no-op and the
      // still-pending first open simply picks up the now-current
      // start_timestamp -- see loggerStartNewPart() in logger.cpp.
      loggerStartNewPart();
    }
  }
}
