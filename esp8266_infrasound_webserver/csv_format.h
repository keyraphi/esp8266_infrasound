// Fixed-width CSV rendering of measurement records.
//
//   time_ms,pressure_pa\n          20 bytes
//   1785926400123,+00.12345\n      24 bytes, every line
//
// Fixed width is required, not cosmetic: it lets a chunked HTTP response map
// an output byte offset back to a record index, so the download is restartable
// and correct.
//
// time_ms is epoch0_ms + t_rel_ms. With no NTP, epoch0_ms is 0 and the column
// is simply relative milliseconds.
//
// Must not include Arduino.h — this header is unit tested natively.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace infrasound {

constexpr size_t   CSV_HEADER_LEN = 20;
constexpr size_t   CSV_LINE_LEN   = 24;
constexpr uint64_t CSV_MAX_TIME   = 9999999999999ull;  // 13 digits
constexpr float    CSV_MAX_VALUE  = 99.99999f;         // 2 integer digits

// Writes CSV_HEADER_LEN bytes plus a terminating NUL. Returns CSV_HEADER_LEN.
inline size_t csvHeader(char* out) {
  std::memcpy(out, "time_ms,pressure_pa\n", CSV_HEADER_LEN + 1);
  return CSV_HEADER_LEN;
}

// Writes exactly CSV_LINE_LEN bytes plus a terminating NUL.
// Returns CSV_LINE_LEN. Never emits a variable-width field.
inline size_t csvLine(uint64_t time_ms, float pa, char* out) {
  if (time_ms > CSV_MAX_TIME) time_ms = CSV_MAX_TIME;

  if (!std::isfinite(pa)) {
    // 6 spaces + "nan" = 9 characters, matching the numeric field width.
    std::snprintf(out, CSV_LINE_LEN + 1, "%013llu,      nan\n",
                  static_cast<unsigned long long>(time_ms));
    return CSV_LINE_LEN;
  }

  if (pa > CSV_MAX_VALUE) pa = CSV_MAX_VALUE;
  if (pa < -CSV_MAX_VALUE) pa = -CSV_MAX_VALUE;

  std::snprintf(out, CSV_LINE_LEN + 1, "%013llu,%+09.5f\n",
                static_cast<unsigned long long>(time_ms),
                static_cast<double>(pa));
  return CSV_LINE_LEN;
}

// Given how many CSV bytes have already been sent, which record comes next.
inline uint32_t csvRecordIndexForOffset(uint64_t output_offset) {
  if (output_offset <= CSV_HEADER_LEN) return 0;
  return static_cast<uint32_t>((output_offset - CSV_HEADER_LEN) / CSV_LINE_LEN);
}

}  // namespace infrasound
