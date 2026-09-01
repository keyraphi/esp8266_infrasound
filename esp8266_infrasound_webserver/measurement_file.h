// On-disk measurement file format.
//
// 32-byte header, then fixed 8-byte records, all little-endian:
//   offset size  field
//     0     8    magic     "INFRASND"
//     8     2    version   uint16 = 1
//    10     2    rec_size  uint16 = 8
//    12     4    rate_hz   uint32 = 50
//    16     8    epoch0_ms uint64  UTC ms at t_rel_ms == 0; 0 if no NTP
//    24     8    reserved  zero
//   record N at 32 + N*8: uint32 t_rel_ms | float32 pressure_pa
//
// Record count is derived from the file size, never stored, so a recording
// ended by unplugging is still fully readable.
//
// Must not include Arduino.h — this header is unit tested natively.
#pragma once

#include <cstdint>
#include <cstring>

namespace infrasound {

constexpr size_t   MF_HEADER_SIZE        = 32;
constexpr size_t   MF_RECORD_SIZE        = 8;
constexpr size_t   MF_LEGACY_RECORD_SIZE = 4;
constexpr uint16_t MF_VERSION            = 1;
constexpr uint32_t MF_RATE_HZ            = 50;
constexpr uint32_t MF_SAMPLE_PERIOD_MS   = 1000 / MF_RATE_HZ;  // 20

struct MeasurementFileHeader {
  uint16_t version;
  uint16_t rec_size;
  uint32_t rate_hz;
  uint64_t epoch0_ms;
};

inline void mfPackHeader(const MeasurementFileHeader& h,
                         uint8_t out[MF_HEADER_SIZE]) {
  std::memset(out, 0, MF_HEADER_SIZE);
  std::memcpy(out + 0, "INFRASND", 8);
  std::memcpy(out + 8, &h.version, 2);
  std::memcpy(out + 10, &h.rec_size, 2);
  std::memcpy(out + 12, &h.rate_hz, 4);
  std::memcpy(out + 16, &h.epoch0_ms, 8);
  // bytes 24..31 remain zero (reserved)
}

// Returns false when the magic is absent, which means a legacy bare-float file.
inline bool mfParseHeader(const uint8_t in[MF_HEADER_SIZE],
                          MeasurementFileHeader* out) {
  if (std::memcmp(in, "INFRASND", 8) != 0) return false;
  std::memcpy(&out->version, in + 8, 2);
  std::memcpy(&out->rec_size, in + 10, 2);
  std::memcpy(&out->rate_hz, in + 12, 4);
  std::memcpy(&out->epoch0_ms, in + 16, 8);
  return true;
}

inline void mfPackRecord(uint32_t t_rel_ms, float v,
                         uint8_t out[MF_RECORD_SIZE]) {
  std::memcpy(out + 0, &t_rel_ms, 4);
  std::memcpy(out + 4, &v, 4);
}

inline void mfParseRecord(const uint8_t in[MF_RECORD_SIZE], uint32_t* t_rel_ms,
                          float* v) {
  std::memcpy(t_rel_ms, in + 0, 4);
  std::memcpy(v, in + 4, 4);
}

inline uint32_t mfRecordCount(uint64_t file_size, bool legacy) {
  if (legacy) return static_cast<uint32_t>(file_size / MF_LEGACY_RECORD_SIZE);
  if (file_size < MF_HEADER_SIZE) return 0;
  return static_cast<uint32_t>((file_size - MF_HEADER_SIZE) / MF_RECORD_SIZE);
}

inline uint32_t mfLegacyTimestamp(uint32_t index) {
  return index * MF_SAMPLE_PERIOD_MS;
}

}  // namespace infrasound
