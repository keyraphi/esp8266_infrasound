#include "logger.h"

#include "diagnostics.h"
#include "measurement_file.h"

extern SdFs sd;
extern uint64_t start_timestamp;
extern uint32_t service_millis_at_t0;
extern infrasound::Diagnostics g_diag;
extern String millisecondsToTimeString(uint64_t milliseconds);

namespace {

// 64 records = 512 bytes, matching SdFat's cache block size so each flush
// fills the cache exactly once. Note this is NOT physical sector alignment:
// the 32-byte file header offsets all records by 32, so writes land at
// 32 + n*512. That costs one extra block re-read after each periodic sync,
// which is negligible against the sync itself on a 400 B/s stream. Aligning
// properly would mean padding the header to 512 bytes, which would ripple
// through the file format, both HTTP endpoints and the browser parser for no
// measurable gain.
constexpr size_t   RECORDS_PER_FLUSH = 64;
constexpr size_t   FLUSH_BYTES       = RECORDS_PER_FLUSH * infrasound::MF_RECORD_SIZE;
constexpr uint32_t SYNC_INTERVAL_MS  = 10000;
constexpr uint32_t ROTATE_AFTER_MS   = 24UL * 60UL * 60UL * 1000UL;

FsFile   g_file;
String   g_file_name;
uint8_t  g_buffer[FLUSH_BYTES];
size_t   g_buffer_used = 0;
uint32_t g_run         = 0;
uint8_t  g_part        = 0;
uint32_t g_last_sync   = 0;
uint32_t g_part_start_t = 0;
bool     g_part_started = false;
bool     g_logging     = false;
// True from loggerBegin() until the first loggerAppend() call actually opens
// a part file. Opening is deferred so the header's filename and epoch0_ms are
// taken from start_timestamp/service_millis_at_t0 as of the first accepted
// sensor frame, not as of setup() -- see loggerEpoch0Ms() in logger.h.
bool     g_open_pending = false;

bool writeBufferToCard() {
  if (g_buffer_used == 0) return true;
  if (!g_file.isOpen()) return false;

  const size_t written = g_file.write(g_buffer, g_buffer_used);
  if (written != g_buffer_used) {
    // Do NOT reset the counter here. The previous implementation cleared it on
    // failure, silently discarding up to 252 samples.
    ++g_diag.sd_write_failures;
    g_logging = false;
    // The part ends on a write failure (spec 8.1): close the handle so the
    // directory entry reflects what actually reached the card.
    g_file.sync();
    g_file.close();
    return false;
  }
  g_buffer_used = 0;
  return true;
}

bool openNewPart() {
  String base;
  if (start_timestamp == 0) {
    base = "/measurements/unbekannt";
  } else {
    base = "/measurements/" + millisecondsToTimeString(start_timestamp);
  }

  // O_EXCL, not O_TRUNC: an existing file must make the open fail rather than
  // be silently truncated. Bounded retry, then give up loudly — overwriting a
  // previous recording is never an acceptable fallback. O_EXCL also closes the
  // check-then-open race the old sd.exists() probe had.
  constexpr uint8_t kMaxPartProbe = 100;
  bool opened = false;
  for (uint8_t probe = 0; probe < kMaxPartProbe; ++probe) {
    char suffix[16];
    snprintf(suffix, sizeof(suffix), "_r%04lu_p%02u",
             static_cast<unsigned long>(g_run), static_cast<unsigned>(g_part));
    g_file_name = base + suffix;
    if (g_file.open(g_file_name.c_str(), O_WRONLY | O_CREAT | O_EXCL)) {
      opened = true;
      break;
    }
    ++g_part;
  }
  if (!opened) {
    ++g_diag.sd_write_failures;
    g_logging = false;
    return false;
  }

  infrasound::MeasurementFileHeader header{};
  header.version   = infrasound::MF_VERSION;
  header.rec_size  = infrasound::MF_RECORD_SIZE;
  header.rate_hz   = infrasound::MF_RATE_HZ;
  // See loggerEpoch0Ms() in logger.h for why this is centralised.
  header.epoch0_ms = loggerEpoch0Ms();

  uint8_t header_bytes[infrasound::MF_HEADER_SIZE];
  infrasound::mfPackHeader(header, header_bytes);
  if (g_file.write(header_bytes, sizeof(header_bytes)) != sizeof(header_bytes)) {
    ++g_diag.sd_write_failures;
    g_file.close();
    g_logging = false;
    return false;
  }

  g_file.sync();
  g_buffer_used  = 0;
  g_part_started = false;
  g_logging      = true;
  return true;
}

}  // namespace

uint64_t loggerEpoch0Ms() {
  return (start_timestamp == 0)
             ? 0
             : start_timestamp + static_cast<uint64_t>(service_millis_at_t0);
}

bool loggerBegin(uint32_t run_number) {
  if (!sd.exists("/measurements") && !sd.mkdir("/measurements")) {
    ++g_diag.sd_write_failures;
    return false;
  }
  g_run  = run_number;
  g_part = 0;
  // Deferred: openNewPart() reads start_timestamp and service_millis_at_t0,
  // neither of which is meaningful yet at setup() time. The first
  // loggerAppend() call opens the part once both are set for real.
  g_open_pending = true;
  return true;
}

void loggerAppend(uint32_t t_rel_ms, float v) {
  if (g_open_pending) {
    g_open_pending = false;
    openNewPart();
  }
  if (!g_logging) return;

  if (!g_part_started) {
    g_part_start_t = t_rel_ms;
    g_part_started = true;
  }

  infrasound::mfPackRecord(t_rel_ms, v, g_buffer + g_buffer_used);
  g_buffer_used += infrasound::MF_RECORD_SIZE;

  if (g_buffer_used >= FLUSH_BYTES) {
    writeBufferToCard();
  }

  if (static_cast<uint32_t>(t_rel_ms - g_part_start_t) >= ROTATE_AFTER_MS) {
    loggerStartNewPart();
  }
}

void loggerTick(uint32_t now_ms) {
  if (!g_logging || !g_file.isOpen()) return;
  if (now_ms - g_last_sync < SYNC_INTERVAL_MS) return;
  g_last_sync = now_ms;
  g_file.sync();
}

void loggerStartNewPart() {
  if (g_open_pending) {
    // No part has been opened yet -- there is nothing to flush/close, and
    // forcing an open now would burn a part number on a file that may end up
    // empty. Leave the flag set: the still-pending open will fire on the
    // first loggerAppend() and pick up whatever start_timestamp is current
    // at that point (this is also how a late-NTP loggerStartNewPart() call
    // races safely with a sensor that hasn't sent its first frame yet).
    return;
  }
  if (g_file.isOpen()) {
    writeBufferToCard();
    // writeBufferToCard() closes the file itself on a write failure; only
    // sync/close here if it's still open, so close ownership stays explicit
    // rather than relying on SdFat no-op'ing an already-closed file.
    if (g_file.isOpen()) {
      g_file.sync();
      g_file.close();
    }
  }
  ++g_part;
  openNewPart();
}

void loggerStop() {
  if (!g_file.isOpen()) return;
  // Flushing on stop is the fix for every recording losing its final
  // up-to-5 seconds, which then reappeared at the start of the next file.
  writeBufferToCard();
  // See loggerStartNewPart(): writeBufferToCard() may have already closed
  // the file on failure, so re-check before syncing/closing again.
  if (g_file.isOpen()) {
    g_file.sync();
    g_file.close();
  }
  g_logging = false;
}

// True once loggerBegin() has armed logging, even before the first part file
// is physically opened. The double-blink NotLogging LED state (see ledTick())
// is reserved for a real failure -- an open that hasn't happened yet because
// no sample has arrived is not one.
bool loggerIsLogging() { return g_logging || g_open_pending; }
const String& loggerFileName() { return g_file_name; }
uint32_t loggerRunNumber() { return g_run; }
uint8_t loggerPartNumber() { return g_part; }

uint32_t readAndIncrementRunCounter() {
  const char* kPath = "/measurements/.run";
  uint32_t value = 0;

  // The counter lives inside /measurements, so ensure the directory exists
  // before trying to write it. loggerBegin() also creates it, but that runs
  // later — on a fresh card the write-back would otherwise fail silently and
  // hand out the same run number again on the next boot.
  if (!sd.exists("/measurements") && !sd.mkdir("/measurements")) {
    ++g_diag.sd_write_failures;
  }

  FsFile f;
  if (f.open(kPath, O_RDONLY)) {
    char buf[16] = {};
    const int n = f.read(buf, sizeof(buf) - 1);
    f.close();
    if (n > 0) {
      value = static_cast<uint32_t>(strtoul(buf, nullptr, 10));
    }
  }

  ++value;
  if (value == 0) value = 1;  // never hand out zero

  if (f.open(kPath, O_WRONLY | O_CREAT | O_TRUNC)) {
    char buf[16];
    const int n = snprintf(buf, sizeof(buf), "%lu",
                           static_cast<unsigned long>(value));
    f.write(buf, n);
    f.sync();
    f.close();
  } else {
    ++g_diag.sd_write_failures;
  }

  return value;
}
