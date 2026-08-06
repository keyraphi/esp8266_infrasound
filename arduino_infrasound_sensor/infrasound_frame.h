// Shared wire protocol between the sensor board and the service module.
//
// CANONICAL COPY. The identical file also lives in
// arduino_infrasound_sensor/infrasound_frame.h because the Arduino IDE cannot
// include headers from outside the sketch folder. Run tools/sync_shared.py
// after editing; tests/test_shared_sync.py fails if the copies drift.
//
// Must not include Arduino.h — this header is unit tested natively.
#pragma once

#include <cstdint>
#include <cstring>

namespace infrasound {

constexpr size_t   FRAME_SIZE  = 11;
constexpr uint8_t  FRAME_START = 0xFF;
constexpr uint8_t  FRAME_END   = 0x00;
constexpr uint8_t  CRC8_POLY   = 0x31;

// CRC-8, polynomial 0x31, init 0x00, MSB-first.
// Same algorithm as SDP600::calcCRC (SDP600.cpp:47-58).
inline uint8_t crc8(const uint8_t* data, size_t len) {
  uint8_t crc = 0;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int bit = 8; bit > 0; --bit) {
      crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ CRC8_POLY)
                         : static_cast<uint8_t>(crc << 1);
    }
  }
  return crc;
}

// Layout: 0xFF | float32 value | uint32 t_ms | crc8(bytes 1..8) | 0x00
inline void encodeFrame(float value, uint32_t t_ms, uint8_t out[FRAME_SIZE]) {
  out[0] = FRAME_START;
  std::memcpy(out + 1, &value, 4);
  std::memcpy(out + 5, &t_ms, 4);
  out[9]  = crc8(out + 1, 8);
  out[10] = FRAME_END;
}

constexpr uint32_t MAX_PLAUSIBLE_GAP_MS = 1000;

struct DecodedFrame {
  bool     ok;
  float    value;
  uint32_t t_ms;
};

// Byte-oriented resynchronising decoder.
//
// A rejected candidate advances the window by exactly one byte, so a valid
// frame beginning inside a corrupted one is still found. This is the
// difference from the previous parser, which consumed a whole frame width on
// a bad end marker.
class FrameDecoder {
 public:
  DecodedFrame feed(uint8_t byte) {
    if (fill_ < FRAME_SIZE) {
      win_[fill_++] = byte;
    } else {
      std::memmove(win_, win_ + 1, FRAME_SIZE - 1);
      win_[FRAME_SIZE - 1] = byte;
    }

    DecodedFrame result{false, 0.0f, 0};
    if (fill_ < FRAME_SIZE) return result;

    if (win_[0] != FRAME_START || win_[FRAME_SIZE - 1] != FRAME_END) {
      return result;  // not a candidate; window advances on the next byte
    }
    if (crc8(win_ + 1, 8) != win_[9]) {
      ++crc_rejects_;
      return result;
    }

    uint32_t t;
    float    v;
    std::memcpy(&v, win_ + 1, 4);
    std::memcpy(&t, win_ + 5, 4);

    if (seeded_) {
      const uint32_t delta = static_cast<uint32_t>(t - last_t_);
      if (delta > MAX_PLAUSIBLE_GAP_MS) {
        ++framing_rejects_;
        return result;
      }
    }

    last_t_ = t;
    seeded_ = true;
    fill_   = 0;

    result.ok    = true;
    result.value = v;
    result.t_ms  = t;
    return result;
  }

  // Accept the next CRC-valid frame regardless of its timestamp, then reseed.
  // Armed at boot, after a detected sensor reboot, and after a silence longer
  // than MAX_PLAUSIBLE_GAP_MS. Never as a fallback for a single failed check.
  void armReseed() { seeded_ = false; }

  void reset() {
    fill_    = 0;
    seeded_  = false;
    last_t_  = 0;
  }

  uint32_t crcRejects()     const { return crc_rejects_; }
  uint32_t framingRejects() const { return framing_rejects_; }
  uint32_t lastAcceptedT()  const { return last_t_; }

 private:
  uint8_t  win_[FRAME_SIZE]  = {};
  size_t   fill_             = 0;
  uint32_t last_t_           = 0;
  bool     seeded_           = false;
  uint32_t crc_rejects_      = 0;
  uint32_t framing_rejects_  = 0;
};

}  // namespace infrasound
