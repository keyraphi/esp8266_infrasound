// Parser for the on-SD-card WiFi credentials file (wifi_ssid_pw.txt).
//
// The file holds one "SSID password" pair per line, in priority order:
// initWifi() walks the parsed list and connects to the first one a scan
// actually sees. Splitting on the LAST space (not the first) lets SSIDs
// contain spaces, which is common, while passwords may not.
//
// A file with exactly two non-blank lines, neither containing a space, is
// the old single-network "SSID\npassword\n" format and is parsed as such
// (see wifiParseCredentials for the exact rule).
//
// Must not include Arduino.h — this header is unit tested natively.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace infrasound {

constexpr size_t WIFI_MAX_NETWORKS = 8;
constexpr size_t WIFI_MAX_SSID_LEN = 33;  // 32 chars + NUL
constexpr size_t WIFI_MAX_PASS_LEN = 64;  // 63 chars + NUL

struct WifiCredential {
  char ssid[WIFI_MAX_SSID_LEN];
  char password[WIFI_MAX_PASS_LEN];
};

struct WifiParseReport {
  size_t parsed = 0;         // credentials written to out
  size_t malformed = 0;      // non-blank lines with no space separator
  size_t over_capacity = 0;  // valid lines dropped because out was full
  bool legacy = false;
};

namespace wifi_detail {

inline bool isBlankChar(char c) { return c == ' ' || c == '\t'; }

// Narrows [begin, end) to exclude leading/trailing spaces and tabs.
inline void trim(const char*& begin, const char*& end) {
  while (begin < end && isBlankChar(*begin)) ++begin;
  while (end > begin && isBlankChar(*(end - 1))) --end;
}

// Copies [begin, end) into out, truncating to fit and always
// NUL-terminating. No-op if out_cap == 0.
inline void copyBounded(const char* begin, const char* end, char* out,
                         size_t out_cap) {
  if (out_cap == 0) return;
  size_t n = static_cast<size_t>(end - begin);
  if (n > out_cap - 1) n = out_cap - 1;
  std::memcpy(out, begin, n);
  out[n] = '\0';
}

// Strips a trailing '\r' (CRLF files) and surrounding whitespace from a raw
// line slice [line_start, raw_end).
inline void normalizeLine(const char* line_start, const char* raw_end,
                           const char*& begin, const char*& end) {
  const char* content_end = raw_end;
  if (content_end > line_start && *(content_end - 1) == '\r') --content_end;
  begin = line_start;
  end = content_end;
  trim(begin, end);
}

// Calls fn(line_begin, line_end) for every line in text — blank or not,
// already \r-stripped and whitespace-trimmed. Handles a final line with no
// trailing newline.
template <typename Fn>
inline void forEachLine(const char* text, Fn&& fn) {
  const char* text_end = text + std::strlen(text);
  const char* p = text;
  while (p < text_end) {
    const char* line_start = p;
    const char* nl =
        static_cast<const char*>(std::memchr(p, '\n', text_end - p));
    const char* raw_end = nl ? nl : text_end;
    p = nl ? nl + 1 : text_end;

    const char* begin;
    const char* end;
    normalizeLine(line_start, raw_end, begin, end);
    fn(begin, end);
  }
}

}  // namespace wifi_detail

// Parses the whole file contents. Returns the number of credentials written
// to `out` (== report->parsed).
//
// Format: one "SSID password" pair per line, in priority order, split at the
// LAST space in the (trimmed, \r-stripped) line. Blank lines are skipped and
// not counted. A non-blank line with no space is malformed and skipped.
// Values longer than their buffer are truncated (still NUL-terminated) and
// still counted as parsed. At most `max` credentials are written; further
// valid lines are counted in over_capacity and dropped.
//
// Legacy format: if the file has exactly two non-blank lines and neither
// contains a space, line 1 is the SSID and line 2 is the password of a
// single network (the old format). report->legacy is set and 1 is returned.
inline size_t wifiParseCredentials(const char* text, WifiCredential* out,
                                    size_t max, WifiParseReport* report) {
  WifiParseReport local_report;
  if (report == nullptr) report = &local_report;
  *report = WifiParseReport();

  if (text == nullptr) return 0;

  // Pass 1: is this the legacy two-line format? Sound because a valid
  // new-format line always contains a space, so a two-line file with no
  // spaces anywhere can only be legacy.
  size_t non_blank_count = 0;
  const char* line1_begin = nullptr;
  const char* line1_end = nullptr;
  bool line1_has_space = false;
  const char* line2_begin = nullptr;
  const char* line2_end = nullptr;
  bool line2_has_space = false;

  wifi_detail::forEachLine(text, [&](const char* b, const char* e) {
    if (b == e) return;  // blank line
    ++non_blank_count;
    bool has_space = false;
    for (const char* q = b; q < e; ++q) {
      if (*q == ' ') {
        has_space = true;
        break;
      }
    }
    if (non_blank_count == 1) {
      line1_begin = b;
      line1_end = e;
      line1_has_space = has_space;
    } else if (non_blank_count == 2) {
      line2_begin = b;
      line2_end = e;
      line2_has_space = has_space;
    }
  });

  const bool legacy =
      (non_blank_count == 2) && !line1_has_space && !line2_has_space;

  if (legacy) {
    report->legacy = true;
    if (max == 0) {
      report->over_capacity = 1;
      return 0;
    }
    wifi_detail::copyBounded(line1_begin, line1_end, out[0].ssid,
                              WIFI_MAX_SSID_LEN);
    wifi_detail::copyBounded(line2_begin, line2_end, out[0].password,
                              WIFI_MAX_PASS_LEN);
    report->parsed = 1;
    return 1;
  }

  // Pass 2: parse each line as "SSID<space>password", split at the last
  // space so SSIDs may contain spaces.
  size_t written = 0;
  wifi_detail::forEachLine(text, [&](const char* b, const char* e) {
    if (b == e) return;  // blank line

    const char* last_space = nullptr;
    for (const char* q = e; q > b;) {
      --q;
      if (*q == ' ') {
        last_space = q;
        break;
      }
    }
    if (last_space == nullptr) {
      ++report->malformed;
      return;
    }

    const char* ssid_begin = b;
    const char* ssid_end = last_space;
    const char* pass_begin = last_space + 1;
    const char* pass_end = e;
    wifi_detail::trim(ssid_begin, ssid_end);
    wifi_detail::trim(pass_begin, pass_end);

    if (written >= max) {
      ++report->over_capacity;
      return;
    }

    wifi_detail::copyBounded(ssid_begin, ssid_end, out[written].ssid,
                              WIFI_MAX_SSID_LEN);
    wifi_detail::copyBounded(pass_begin, pass_end, out[written].password,
                              WIFI_MAX_PASS_LEN);
    ++written;
    ++report->parsed;
  });

  return written;
}

}  // namespace infrasound
