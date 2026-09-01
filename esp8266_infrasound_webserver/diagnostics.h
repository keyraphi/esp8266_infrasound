// Permanent instrumentation for the service module.
//
// These counters exist because "we are losing data and cannot tell" is the
// failure mode this whole rework addresses. A one-line summary goes to the
// debug serial every 60 s.
//
// Must not include Arduino.h — this header is unit tested natively.
#pragma once

#include <cstdint>
#include <cstdio>

namespace infrasound {

struct Diagnostics {
  uint32_t serial_overflows        = 0;
  uint32_t frames_crc_rejected     = 0;
  uint32_t frames_framing_rejected = 0;
  uint32_t queue_full_drops        = 0;
  uint32_t sd_write_failures       = 0;
  uint32_t max_loop_interval_ms    = 0;
  uint32_t min_free_heap           = 0;
  uint32_t ntp_failures            = 0;
  uint32_t sensor_reboots          = 0;
};

// True when no counter indicating data loss or failure has fired.
// max_loop_interval_ms and min_free_heap are observations, not errors.
inline bool diagnosticsAllClear(const Diagnostics& d) {
  return d.serial_overflows == 0 && d.frames_crc_rejected == 0 &&
         d.frames_framing_rejected == 0 && d.queue_full_drops == 0 &&
         d.sd_write_failures == 0 && d.ntp_failures == 0 &&
         d.sensor_reboots == 0;
}

// Returns the number of characters written, always NUL terminated.
inline size_t formatDiagnostics(const Diagnostics& d, char* out, size_t cap) {
  const int n = std::snprintf(
      out, cap,
      "DIAG ovf=%lu crc=%lu frm=%lu qfull=%lu sdfail=%lu maxloop=%lu "
      "heap=%lu ntpfail=%lu reboots=%lu",
      static_cast<unsigned long>(d.serial_overflows),
      static_cast<unsigned long>(d.frames_crc_rejected),
      static_cast<unsigned long>(d.frames_framing_rejected),
      static_cast<unsigned long>(d.queue_full_drops),
      static_cast<unsigned long>(d.sd_write_failures),
      static_cast<unsigned long>(d.max_loop_interval_ms),
      static_cast<unsigned long>(d.min_free_heap),
      static_cast<unsigned long>(d.ntp_failures),
      static_cast<unsigned long>(d.sensor_reboots));
  if (n < 0) {
    if (cap > 0) out[0] = '\0';
    return 0;
  }
  if (static_cast<size_t>(n) >= cap) return cap > 0 ? cap - 1 : 0;
  return static_cast<size_t>(n);
}

}  // namespace infrasound
