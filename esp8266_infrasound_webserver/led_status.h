// Onboard LED patterns for the service module.
//
// LED_BUILTIN is GPIO2 (D4), active LOW, and free on this board: the link uses
// GPIO4/5 and SPI uses GPIO12-15. GPIO2 is a boot-strap pin that must be HIGH
// at reset, so it is only driven as an output after setup() begins.
//
// The heartbeat means "powered and time set", not "currently logging" and not
// "receiving data". A measurement stopped deliberately for analysis mode gets
// its own slow even blink (Stopped); a sensor that has gone silent while a
// measurement is supposedly running gets its own triple flash (NoData); the
// double blink stays reserved for actual write failures.
//
// Must not include Arduino.h — this header is unit tested natively.
#pragma once

#include <cstdint>

namespace infrasound {

enum class LedState {
  SdFailure,   // solid on: stuck in the SD init retry loop
  NotLogging,  // double blink: SD write failure or card full
  NoData,      // triple flash: measurement running, but no sensor data
  NoTime,      // fast blink 5 Hz: running, no NTP
  Stopped,     // slow even blink: measurement stopped deliberately
  Ok,          // short flash every 2 s: running, time set
};

// Highest-priority state wins; the caller decides which state applies.
inline bool ledOn(LedState state, uint32_t now_ms) {
  switch (state) {
    case LedState::SdFailure:
      return true;

    case LedState::NotLogging: {
      const uint32_t p = now_ms % 2000;
      return (p < 100) || (p >= 250 && p < 350);
    }

    case LedState::NoData: {
      const uint32_t p = now_ms % 2000;
      return (p < 100) || (p >= 250 && p < 350) || (p >= 500 && p < 600);
    }

    case LedState::NoTime:
      return (now_ms % 200) < 100;

    case LedState::Stopped:
      return (now_ms % 2000) < 1000;

    case LedState::Ok:
      return (now_ms % 2000) < 50;
  }
  return false;
}

}  // namespace infrasound
