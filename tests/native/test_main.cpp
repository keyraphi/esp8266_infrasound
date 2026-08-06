#include "test_assert.h"
#include "infrasound_frame.h"
#include "measurement_file.h"
#include "csv_format.h"
#include "led_status.h"
#include "diagnostics.h"
#include "static_manifest.h"
#include "wifi_credentials.h"

static void test_harness_works() {
  CHECK(1 + 1 == 2);
  CHECK_EQ(2 + 2, 4);
  CHECK_STR_EQ("abc", "abc");
}

static void test_crc8_matches_sdp600_algorithm() {
  // Reference values computed with polynomial 0x31, init 0x00, MSB-first —
  // the same algorithm as SDP600::calcCRC in SDP600.cpp:47-58.
  const uint8_t empty[1] = {0x00};
  CHECK_EQ(infrasound::crc8(empty, 0), 0x00);

  const uint8_t one_zero[1] = {0x00};
  CHECK_EQ(infrasound::crc8(one_zero, 1), 0x00);

  const uint8_t one_ff[1] = {0xFF};
  CHECK_EQ(infrasound::crc8(one_ff, 1), 0xAC);

  const uint8_t seq[4] = {0x01, 0x02, 0x03, 0x04};
  CHECK_EQ(infrasound::crc8(seq, 4), 0xFE);
}

static void test_encode_frame_layout() {
  uint8_t f[infrasound::FRAME_SIZE];
  infrasound::encodeFrame(-1.9999f, 0x01020304u, f);

  CHECK_EQ(f[0], infrasound::FRAME_START);
  CHECK_EQ(f[10], infrasound::FRAME_END);

  // little-endian float32 of -1.9999f is B9 FC FF BF
  CHECK_EQ(f[1], 0xB9);
  CHECK_EQ(f[2], 0xFC);
  CHECK_EQ(f[3], 0xFF);
  CHECK_EQ(f[4], 0xBF);

  // little-endian uint32 of 0x01020304
  CHECK_EQ(f[5], 0x04);
  CHECK_EQ(f[6], 0x03);
  CHECK_EQ(f[7], 0x02);
  CHECK_EQ(f[8], 0x01);

  CHECK_EQ(f[9], infrasound::crc8(f + 1, 8));
}

static void test_encode_frame_payload_can_contain_marker_bytes() {
  // This is the whole reason the frame needs a CRC: a legitimate pressure
  // value contains 0xFF, and a small timestamp contains 0x00.
  uint8_t f[infrasound::FRAME_SIZE];
  infrasound::encodeFrame(-1.9999f, 30u, f);
  CHECK_EQ(f[3], 0xFF);
  CHECK_EQ(f[8], 0x00);
}

static void feedAll(infrasound::FrameDecoder& d, const uint8_t* bytes, size_t n,
                    infrasound::DecodedFrame* results, size_t* count) {
  *count = 0;
  for (size_t i = 0; i < n; ++i) {
    infrasound::DecodedFrame r = d.feed(bytes[i]);
    if (r.ok) results[(*count)++] = r;
  }
}

static void test_decoder_accepts_a_clean_frame() {
  uint8_t f[infrasound::FRAME_SIZE];
  infrasound::encodeFrame(1.25f, 1000u, f);

  infrasound::FrameDecoder d;
  infrasound::DecodedFrame out[4];
  size_t n = 0;
  feedAll(d, f, infrasound::FRAME_SIZE, out, &n);

  CHECK_EQ(n, 1u);
  CHECK(out[0].value == 1.25f);
  CHECK_EQ(out[0].t_ms, 1000u);
}

static void test_decoder_rejects_corrupted_crc() {
  uint8_t f[infrasound::FRAME_SIZE];
  infrasound::encodeFrame(1.25f, 1000u, f);
  f[2] ^= 0xFF;  // corrupt the payload, leaving markers intact

  infrasound::FrameDecoder d;
  infrasound::DecodedFrame out[4];
  size_t n = 0;
  feedAll(d, f, infrasound::FRAME_SIZE, out, &n);

  CHECK_EQ(n, 0u);
  CHECK_EQ(d.crcRejects(), 1u);
}

static void test_corrupted_frame_does_not_swallow_the_next_valid_one() {
  // THE REGRESSION TEST. The old parser consumed a whole frame width on a bad
  // candidate, destroying any valid frame that began inside it.
  //
  // The stream is a TRUNCATED frame (7 of 11 bytes, as a serial buffer
  // overflow would leave) followed by a complete valid frame. The good frame
  // therefore does NOT start at a frame-width boundary from the start of the
  // partial one, so a decoder that discards a whole window on rejection loses
  // it. Only byte-at-a-time resynchronisation recovers it.
  uint8_t partial[infrasound::FRAME_SIZE];
  uint8_t good[infrasound::FRAME_SIZE];
  infrasound::encodeFrame(1.25f, 1000u, partial);
  infrasound::encodeFrame(2.50f, 1020u, good);

  uint8_t stream[7 + infrasound::FRAME_SIZE];
  std::memcpy(stream, partial, 7);
  std::memcpy(stream + 7, good, infrasound::FRAME_SIZE);

  infrasound::FrameDecoder d;
  infrasound::DecodedFrame out[4];
  size_t n = 0;
  feedAll(d, stream, sizeof(stream), out, &n);

  CHECK_EQ(n, 1u);
  CHECK(out[0].value == 2.50f);
  CHECK_EQ(out[0].t_ms, 1020u);
}

static void test_decoder_resyncs_after_arbitrary_garbage() {
  uint8_t good[infrasound::FRAME_SIZE];
  infrasound::encodeFrame(-3.5f, 5000u, good);

  uint8_t stream[7 + infrasound::FRAME_SIZE];
  const uint8_t garbage[7] = {0xFF, 0x00, 0xFF, 0xAA, 0x00, 0xFF, 0x13};
  std::memcpy(stream, garbage, 7);
  std::memcpy(stream + 7, good, infrasound::FRAME_SIZE);

  infrasound::FrameDecoder d;
  infrasound::DecodedFrame out[4];
  size_t n = 0;
  feedAll(d, stream, sizeof(stream), out, &n);

  CHECK_EQ(n, 1u);
  CHECK(out[0].value == -3.5f);
}

static void test_decoder_rejects_implausible_timestamp() {
  uint8_t a[infrasound::FRAME_SIZE];
  uint8_t b[infrasound::FRAME_SIZE];
  infrasound::encodeFrame(1.0f, 1000u, a);
  infrasound::encodeFrame(2.0f, 9000u, b);  // 8000 ms jump, > 1000 ms window

  infrasound::FrameDecoder d;
  infrasound::DecodedFrame out[4];
  size_t n = 0;
  feedAll(d, a, infrasound::FRAME_SIZE, out, &n);
  CHECK_EQ(n, 1u);

  feedAll(d, b, infrasound::FRAME_SIZE, out, &n);
  CHECK_EQ(n, 0u);
  CHECK_EQ(d.framingRejects(), 1u);
}

static void test_first_frame_is_accepted_without_a_reference() {
  // last_t is undefined at boot; the first CRC-valid frame seeds it.
  uint8_t f[infrasound::FRAME_SIZE];
  infrasound::encodeFrame(1.0f, 123456789u, f);

  infrasound::FrameDecoder d;
  infrasound::DecodedFrame out[4];
  size_t n = 0;
  feedAll(d, f, infrasound::FRAME_SIZE, out, &n);

  CHECK_EQ(n, 1u);
  CHECK_EQ(d.lastAcceptedT(), 123456789u);
}

static void test_arm_reseed_accepts_a_discontinuous_timestamp() {
  uint8_t a[infrasound::FRAME_SIZE];
  uint8_t b[infrasound::FRAME_SIZE];
  infrasound::encodeFrame(1.0f, 5000000u, a);
  infrasound::encodeFrame(2.0f, 30u, b);  // sensor rebooted

  infrasound::FrameDecoder d;
  infrasound::DecodedFrame out[4];
  size_t n = 0;
  feedAll(d, a, infrasound::FRAME_SIZE, out, &n);
  CHECK_EQ(n, 1u);

  feedAll(d, b, infrasound::FRAME_SIZE, out, &n);
  CHECK_EQ(n, 0u);  // rejected while seeded

  d.armReseed();
  feedAll(d, b, infrasound::FRAME_SIZE, out, &n);
  CHECK_EQ(n, 1u);
  CHECK_EQ(out[0].t_ms, 30u);
}

static void test_plausibility_survives_the_millis_wrap() {
  const uint32_t before = 0xFFFFFFF0u;  // 16 ms before wrap
  const uint32_t after  = 0x00000004u;  // 20 ms later, wrapped

  uint8_t a[infrasound::FRAME_SIZE];
  uint8_t b[infrasound::FRAME_SIZE];
  infrasound::encodeFrame(1.0f, before, a);
  infrasound::encodeFrame(2.0f, after, b);

  infrasound::FrameDecoder d;
  infrasound::DecodedFrame out[4];
  size_t n = 0;
  feedAll(d, a, infrasound::FRAME_SIZE, out, &n);
  CHECK_EQ(n, 1u);

  feedAll(d, b, infrasound::FRAME_SIZE, out, &n);
  CHECK_EQ(n, 1u);  // unsigned subtraction gives 20, not a huge number
  CHECK_EQ(out[0].t_ms, after);
}

static void test_header_round_trip() {
  infrasound::MeasurementFileHeader in{};
  in.version   = infrasound::MF_VERSION;
  in.rec_size  = infrasound::MF_RECORD_SIZE;
  in.rate_hz   = infrasound::MF_RATE_HZ;
  in.epoch0_ms = 1785926400123ull;

  uint8_t buf[infrasound::MF_HEADER_SIZE];
  infrasound::mfPackHeader(in, buf);

  CHECK_EQ(std::memcmp(buf, "INFRASND", 8), 0);

  infrasound::MeasurementFileHeader out{};
  CHECK(infrasound::mfParseHeader(buf, &out));
  CHECK_EQ(out.version, in.version);
  CHECK_EQ(out.rec_size, in.rec_size);
  CHECK_EQ(out.rate_hz, in.rate_hz);
  CHECK(out.epoch0_ms == in.epoch0_ms);
}

static void test_header_reserved_bytes_are_zero() {
  infrasound::MeasurementFileHeader in{};
  in.version = 1; in.rec_size = 8; in.rate_hz = 50; in.epoch0_ms = 0;
  uint8_t buf[infrasound::MF_HEADER_SIZE];
  std::memset(buf, 0xAA, sizeof(buf));
  infrasound::mfPackHeader(in, buf);
  for (size_t i = 24; i < 32; ++i) CHECK_EQ(buf[i], 0);
}

static void test_parse_header_rejects_missing_magic() {
  uint8_t buf[infrasound::MF_HEADER_SIZE] = {};
  std::memcpy(buf, "NOTMAGIC", 8);
  infrasound::MeasurementFileHeader out{};
  CHECK(!infrasound::mfParseHeader(buf, &out));
}

static void test_record_round_trip() {
  uint8_t rec[infrasound::MF_RECORD_SIZE];
  infrasound::mfPackRecord(86399999u, -27.3f, rec);

  uint32_t t = 0;
  float    v = 0.0f;
  infrasound::mfParseRecord(rec, &t, &v);
  CHECK_EQ(t, 86399999u);
  CHECK(v == -27.3f);
}

static void test_record_count_is_derived_from_file_size() {
  CHECK_EQ(infrasound::mfRecordCount(32, false), 0u);
  CHECK_EQ(infrasound::mfRecordCount(32 + 8, false), 1u);
  CHECK_EQ(infrasound::mfRecordCount(32 + 8 * 4320000ull, false), 4320000u);
  // A truncated tail (power loss mid-record) is not counted.
  CHECK_EQ(infrasound::mfRecordCount(32 + 8 * 3 + 5, false), 3u);
  // Smaller than a header at all.
  CHECK_EQ(infrasound::mfRecordCount(10, false), 0u);
}

static void test_legacy_file_record_count_and_timestamps() {
  // Legacy: bare 4-byte floats, no header, uniform 50 Hz.
  CHECK_EQ(infrasound::mfRecordCount(4 * 1000, true), 1000u);
  CHECK_EQ(infrasound::mfLegacyTimestamp(0), 0u);
  CHECK_EQ(infrasound::mfLegacyTimestamp(1), 20u);
  CHECK_EQ(infrasound::mfLegacyTimestamp(50), 1000u);
}

static void test_csv_header_is_exactly_twenty_bytes() {
  char buf[64];
  const size_t n = infrasound::csvHeader(buf);
  CHECK_EQ(n, infrasound::CSV_HEADER_LEN);
  CHECK_STR_EQ(buf, "time_ms,pressure_pa\n");
}

static void test_csv_line_is_exactly_twenty_four_bytes_across_the_range() {
  char buf[64];
  const float values[] = {0.0f,     0.00083f, -0.00083f, 1.0f,   -1.0f,
                          27.3f,    -27.3f,   9.99999f,  -9.99999f, 0.5f};
  for (float v : values) {
    const size_t n = infrasound::csvLine(1785926400123ull, v, buf);
    CHECK_EQ(n, infrasound::CSV_LINE_LEN);
    CHECK_EQ(std::strlen(buf), infrasound::CSV_LINE_LEN);
    CHECK_EQ(buf[infrasound::CSV_LINE_LEN - 1], '\n');
  }
}

static void test_csv_line_exact_content() {
  char buf[64];
  infrasound::csvLine(1785926400123ull, 0.12345f, buf);
  CHECK_STR_EQ(buf, "1785926400123,+00.12345\n");

  infrasound::csvLine(0ull, -27.3f, buf);
  CHECK_STR_EQ(buf, "0000000000000,-27.30000\n");
}

static void test_csv_line_pads_small_timestamps_to_thirteen_digits() {
  char buf[64];
  infrasound::csvLine(20ull, 1.0f, buf);
  CHECK_STR_EQ(buf, "0000000000020,+01.00000\n");
}

static void test_csv_line_handles_non_finite_at_fixed_width() {
  char buf[64];
  const float nan_value = std::nanf("");
  const size_t n = infrasound::csvLine(1000ull, nan_value, buf);
  CHECK_EQ(n, infrasound::CSV_LINE_LEN);
  CHECK_STR_EQ(buf, "0000000001000,      nan\n");
}

static void test_csv_line_clamps_out_of_range_values() {
  // The sensor cannot produce these, but the writer must never emit a
  // variable-width line.
  char buf[64];
  CHECK_EQ(infrasound::csvLine(1000ull, 1234.5f, buf), infrasound::CSV_LINE_LEN);
  CHECK_EQ(infrasound::csvLine(1000ull, -1234.5f, buf), infrasound::CSV_LINE_LEN);
  CHECK_EQ(infrasound::csvLine(99999999999999ull, 1.0f, buf),
           infrasound::CSV_LINE_LEN);
}

static void test_output_offset_maps_to_record_index() {
  CHECK_EQ(infrasound::csvRecordIndexForOffset(20), 0u);
  CHECK_EQ(infrasound::csvRecordIndexForOffset(20 + 24), 1u);
  CHECK_EQ(infrasound::csvRecordIndexForOffset(20 + 24 * 1000), 1000u);
  CHECK_EQ(infrasound::csvRecordIndexForOffset(0), 0u);
}

static void test_sd_failure_is_solid_on() {
  CHECK(infrasound::ledOn(infrasound::LedState::SdFailure, 0));
  CHECK(infrasound::ledOn(infrasound::LedState::SdFailure, 12345));
  CHECK(infrasound::ledOn(infrasound::LedState::SdFailure, 999999));
}

static void test_no_time_blinks_at_five_hertz() {
  // 200 ms period, on for the first 100 ms.
  CHECK(infrasound::ledOn(infrasound::LedState::NoTime, 0));
  CHECK(infrasound::ledOn(infrasound::LedState::NoTime, 99));
  CHECK(!infrasound::ledOn(infrasound::LedState::NoTime, 100));
  CHECK(!infrasound::ledOn(infrasound::LedState::NoTime, 199));
  CHECK(infrasound::ledOn(infrasound::LedState::NoTime, 200));
}

static void test_ok_is_a_short_flash_every_two_seconds() {
  CHECK(infrasound::ledOn(infrasound::LedState::Ok, 0));
  CHECK(infrasound::ledOn(infrasound::LedState::Ok, 49));
  CHECK(!infrasound::ledOn(infrasound::LedState::Ok, 50));
  CHECK(!infrasound::ledOn(infrasound::LedState::Ok, 1999));
  CHECK(infrasound::ledOn(infrasound::LedState::Ok, 2000));
}

static void test_not_logging_is_a_double_blink() {
  // 2000 ms period: on 0-100, off 100-250, on 250-350, off 350-2000.
  CHECK(infrasound::ledOn(infrasound::LedState::NotLogging, 0));
  CHECK(infrasound::ledOn(infrasound::LedState::NotLogging, 99));
  CHECK(!infrasound::ledOn(infrasound::LedState::NotLogging, 100));
  CHECK(!infrasound::ledOn(infrasound::LedState::NotLogging, 249));
  CHECK(infrasound::ledOn(infrasound::LedState::NotLogging, 250));
  CHECK(infrasound::ledOn(infrasound::LedState::NotLogging, 349));
  CHECK(!infrasound::ledOn(infrasound::LedState::NotLogging, 350));
  CHECK(!infrasound::ledOn(infrasound::LedState::NotLogging, 1999));
  CHECK(infrasound::ledOn(infrasound::LedState::NotLogging, 2000));
}

static void test_no_data_is_a_triple_flash() {
  // 2000 ms period: on 0-100, off 100-250, on 250-350, off 350-500,
  // on 500-600, off 600-2000.
  CHECK(infrasound::ledOn(infrasound::LedState::NoData, 0));
  CHECK(infrasound::ledOn(infrasound::LedState::NoData, 99));
  CHECK(!infrasound::ledOn(infrasound::LedState::NoData, 100));
  CHECK(!infrasound::ledOn(infrasound::LedState::NoData, 249));
  CHECK(infrasound::ledOn(infrasound::LedState::NoData, 250));
  CHECK(infrasound::ledOn(infrasound::LedState::NoData, 349));
  CHECK(!infrasound::ledOn(infrasound::LedState::NoData, 350));
  CHECK(!infrasound::ledOn(infrasound::LedState::NoData, 499));
  CHECK(infrasound::ledOn(infrasound::LedState::NoData, 500));
  CHECK(infrasound::ledOn(infrasound::LedState::NoData, 599));
  CHECK(!infrasound::ledOn(infrasound::LedState::NoData, 600));
  CHECK(!infrasound::ledOn(infrasound::LedState::NoData, 1999));
  CHECK(infrasound::ledOn(infrasound::LedState::NoData, 2000));
}

static void test_stopped_is_a_slow_even_blink() {
  // 2000 ms period: on 0-1000, off 1000-2000.
  CHECK(infrasound::ledOn(infrasound::LedState::Stopped, 0));
  CHECK(infrasound::ledOn(infrasound::LedState::Stopped, 999));
  CHECK(!infrasound::ledOn(infrasound::LedState::Stopped, 1000));
  CHECK(!infrasound::ledOn(infrasound::LedState::Stopped, 1999));
  CHECK(infrasound::ledOn(infrasound::LedState::Stopped, 2000));
}

static void test_patterns_are_distinguishable_by_duty_cycle() {
  // A human must be able to tell these apart at a glance, so assert the
  // number of on-samples over one 2000 ms window differs materially.
  int not_logging = 0, no_time = 0, ok = 0;
  for (uint32_t t = 0; t < 2000; ++t) {
    if (infrasound::ledOn(infrasound::LedState::NotLogging, t)) ++not_logging;
    if (infrasound::ledOn(infrasound::LedState::NoTime, t)) ++no_time;
    if (infrasound::ledOn(infrasound::LedState::Ok, t)) ++ok;
  }
  CHECK_EQ(not_logging, 200);
  CHECK_EQ(no_time, 1000);
  CHECK_EQ(ok, 50);
}

static void test_all_six_states_are_distinguishable() {
  // Neither on-ms duty nor transition count alone separates all six states
  // (NoTime and Stopped share duty; Stopped and Ok share transition count),
  // so assert the (on_ms, transitions) pair is unique across all six.
  const infrasound::LedState states[] = {
      infrasound::LedState::SdFailure,  infrasound::LedState::NotLogging,
      infrasound::LedState::NoData,     infrasound::LedState::NoTime,
      infrasound::LedState::Stopped,    infrasound::LedState::Ok,
  };
  const int expected_on_ms[] = {2000, 200, 300, 1000, 1000, 50};
  const int expected_transitions[] = {0, 4, 6, 20, 2, 2};
  constexpr int n = 6;

  int on_ms[n] = {};
  int transitions[n] = {};
  for (int i = 0; i < n; ++i) {
    bool prev = infrasound::ledOn(states[i], 1999);  // wrap from t=1999
    for (uint32_t t = 0; t < 2000; ++t) {
      const bool cur = infrasound::ledOn(states[i], t);
      if (cur) ++on_ms[i];
      if (cur != prev) ++transitions[i];
      prev = cur;
    }
    CHECK_EQ(on_ms[i], expected_on_ms[i]);
    CHECK_EQ(transitions[i], expected_transitions[i]);
  }

  for (int i = 0; i < n; ++i) {
    for (int j = i + 1; j < n; ++j) {
      CHECK(on_ms[i] != on_ms[j] || transitions[i] != transitions[j]);
    }
  }
}

static void test_diagnostics_all_clear() {
  infrasound::Diagnostics d{};
  d.min_free_heap = 20000;       // not an error signal
  d.max_loop_interval_ms = 12;   // not an error signal
  CHECK(infrasound::diagnosticsAllClear(d));

  d.serial_overflows = 1;
  CHECK(!infrasound::diagnosticsAllClear(d));

  // Each error field must independently defeat all-clear. A regression that
  // dropped one from the chain would otherwise pass unnoticed.
  {
    infrasound::Diagnostics base{};
    base.min_free_heap = 20000;
    base.max_loop_interval_ms = 12;

    infrasound::Diagnostics d1 = base; d1.serial_overflows = 1;
    CHECK(!infrasound::diagnosticsAllClear(d1));
    infrasound::Diagnostics d2 = base; d2.frames_crc_rejected = 1;
    CHECK(!infrasound::diagnosticsAllClear(d2));
    infrasound::Diagnostics d3 = base; d3.frames_framing_rejected = 1;
    CHECK(!infrasound::diagnosticsAllClear(d3));
    infrasound::Diagnostics d4 = base; d4.queue_full_drops = 1;
    CHECK(!infrasound::diagnosticsAllClear(d4));
    infrasound::Diagnostics d5 = base; d5.sd_write_failures = 1;
    CHECK(!infrasound::diagnosticsAllClear(d5));
    infrasound::Diagnostics d6 = base; d6.ntp_failures = 1;
    CHECK(!infrasound::diagnosticsAllClear(d6));
    infrasound::Diagnostics d7 = base; d7.sensor_reboots = 1;
    CHECK(!infrasound::diagnosticsAllClear(d7));
  }
}

static void test_format_diagnostics_includes_every_counter() {
  infrasound::Diagnostics d{};
  d.serial_overflows       = 1;
  d.frames_crc_rejected    = 2;
  d.frames_framing_rejected= 3;
  d.queue_full_drops       = 4;
  d.sd_write_failures      = 5;
  d.max_loop_interval_ms   = 6;
  d.min_free_heap          = 7;
  d.ntp_failures           = 8;
  d.sensor_reboots         = 9;

  char buf[256];
  const size_t n = infrasound::formatDiagnostics(d, buf, sizeof(buf));
  CHECK(n > 0);
  CHECK(n < sizeof(buf));
  CHECK(std::strncmp(buf, "DIAG ", 5) == 0);
  CHECK(std::strstr(buf, "ovf=1") != nullptr);
  CHECK(std::strstr(buf, "crc=2") != nullptr);
  CHECK(std::strstr(buf, "frm=3") != nullptr);
  CHECK(std::strstr(buf, "qfull=4") != nullptr);
  CHECK(std::strstr(buf, "sdfail=5") != nullptr);
  CHECK(std::strstr(buf, "maxloop=6") != nullptr);
  CHECK(std::strstr(buf, "heap=7") != nullptr);
  CHECK(std::strstr(buf, "ntpfail=8") != nullptr);
  CHECK(std::strstr(buf, "reboots=9") != nullptr);
}

static void test_format_diagnostics_respects_a_small_buffer() {
  infrasound::Diagnostics d{};
  char buf[8];
  const size_t n = infrasound::formatDiagnostics(d, buf, sizeof(buf));
  CHECK(n < sizeof(buf));
  CHECK_EQ(buf[sizeof(buf) - 1], '\0');
}

static void test_manifest_entry_round_trip() {
  infrasound::ManifestEntry in{};
  std::snprintf(in.path, sizeof(in.path), "/highcharts/highcharts.js");
  in.size     = 278596;
  in.fat_date = 0x5904;
  in.fat_time = 0x7B21;
  in.crc32    = 0xDEADBEEFu;

  uint8_t buf[infrasound::SM_ENTRY_SIZE];
  infrasound::smPackEntry(in, buf);

  infrasound::ManifestEntry out{};
  CHECK(infrasound::smParseEntry(buf, &out));
  CHECK_STR_EQ(out.path, in.path);
  CHECK_EQ(out.size, in.size);
  CHECK_EQ(out.fat_date, in.fat_date);
  CHECK_EQ(out.fat_time, in.fat_time);
  CHECK_EQ(out.crc32, in.crc32);
}

static void test_manifest_rejects_an_unterminated_path() {
  uint8_t buf[infrasound::SM_ENTRY_SIZE];
  std::memset(buf, 'A', sizeof(buf));  // no NUL anywhere in the path field
  infrasound::ManifestEntry out{};
  CHECK(!infrasound::smParseEntry(buf, &out));
}

static void test_needs_copy_detects_changes() {
  infrasound::ManifestEntry stored{};
  std::snprintf(stored.path, sizeof(stored.path), "/index.html");
  stored.size     = 8755;
  stored.fat_date = 0x5904;
  stored.fat_time = 0x7B21;

  // Identical: skip.
  CHECK(!infrasound::smNeedsCopy(stored, 8755, 0x5904, 0x7B21));
  // Size changed.
  CHECK(infrasound::smNeedsCopy(stored, 8756, 0x5904, 0x7B21));
  // Date changed.
  CHECK(infrasound::smNeedsCopy(stored, 8755, 0x5905, 0x7B21));
  // Time changed.
  CHECK(infrasound::smNeedsCopy(stored, 8755, 0x5904, 0x7B22));
}

static void test_crc32_is_streamable_and_matches_known_vector() {
  const char* s = "123456789";
  const uint32_t whole =
      infrasound::smCrc32(reinterpret_cast<const uint8_t*>(s), 9, 0);
  CHECK_EQ(whole, 0xCBF43926u);  // standard CRC-32 check value

  // Streaming in two pieces must equal the one-shot result, because the boot
  // sync computes the CRC while copying in chunks.
  const uint32_t part =
      infrasound::smCrc32(reinterpret_cast<const uint8_t*>(s), 4, 0);
  const uint32_t rest =
      infrasound::smCrc32(reinterpret_cast<const uint8_t*>(s) + 4, 5, part);
  CHECK_EQ(rest, whole);
}

static void test_manifest_entry_raw_byte_layout() {
  // Pins the on-disk contract independently of pack/parse symmetry: a
  // matched pair of shifted offsets would round-trip fine while writing a
  // manifest that does not match the documented layout.
  infrasound::ManifestEntry in{};
  std::snprintf(in.path, sizeof(in.path), "/x.js");
  in.size     = 0x11223344u;
  in.fat_date = 0x5566;
  in.fat_time = 0x7788;
  in.crc32    = 0x99AABBCCu;

  uint8_t buf[infrasound::SM_ENTRY_SIZE];
  std::memset(buf, 0xEE, sizeof(buf));
  infrasound::smPackEntry(in, buf);

  CHECK_EQ(std::memcmp(buf, "/x.js", 6), 0);      // path + its NUL at offset 0
  CHECK_EQ(buf[48], 0x44); CHECK_EQ(buf[49], 0x33);
  CHECK_EQ(buf[50], 0x22); CHECK_EQ(buf[51], 0x11);   // size @48, little-endian
  CHECK_EQ(buf[52], 0x66); CHECK_EQ(buf[53], 0x55);   // fat_date @52
  CHECK_EQ(buf[54], 0x88); CHECK_EQ(buf[55], 0x77);   // fat_time @54
  CHECK_EQ(buf[56], 0xCC); CHECK_EQ(buf[57], 0xBB);
  CHECK_EQ(buf[58], 0xAA); CHECK_EQ(buf[59], 0x99);   // crc32 @56
  for (size_t i = 60; i < 64; ++i) CHECK_EQ(buf[i], 0);  // reserved
  for (size_t i = 6; i < 48; ++i) CHECK_EQ(buf[i], 0);   // path tail deterministic
}

static void test_path_fits() {
  // Paths under the limit fit.
  CHECK(infrasound::smPathFits(""));
  CHECK(infrasound::smPathFits("/short.txt"));
  // Exactly 47 characters fits (leaves room for NUL).
  char path47[48];
  std::memset(path47, 'a', 47);
  path47[47] = '\0';
  CHECK(infrasound::smPathFits(path47));
  // Exactly 48 characters does not fit.
  char path48[49];
  std::memset(path48, 'a', 48);
  path48[48] = '\0';
  CHECK(!infrasound::smPathFits(path48));
}

static void test_wifi_parses_normal_multiline_file_in_order() {
  const char* text =
      "NetworkA passwordA\nNetworkB passwordB\nNetworkC passwordC\n";
  infrasound::WifiCredential out[infrasound::WIFI_MAX_NETWORKS];
  infrasound::WifiParseReport report;
  size_t n = infrasound::wifiParseCredentials(
      text, out, infrasound::WIFI_MAX_NETWORKS, &report);
  CHECK_EQ(n, 3u);
  CHECK_EQ(report.parsed, 3u);
  CHECK_EQ(report.malformed, 0u);
  CHECK_EQ(report.over_capacity, 0u);
  CHECK(!report.legacy);
  CHECK_STR_EQ(out[0].ssid, "NetworkA");
  CHECK_STR_EQ(out[0].password, "passwordA");
  CHECK_STR_EQ(out[1].ssid, "NetworkB");
  CHECK_STR_EQ(out[1].password, "passwordB");
  CHECK_STR_EQ(out[2].ssid, "NetworkC");
  CHECK_STR_EQ(out[2].password, "passwordC");
}

static void test_wifi_ssid_can_contain_spaces() {
  const char* text = "My Home Network hunter2\n";
  infrasound::WifiCredential out[infrasound::WIFI_MAX_NETWORKS];
  infrasound::WifiParseReport report;
  size_t n = infrasound::wifiParseCredentials(
      text, out, infrasound::WIFI_MAX_NETWORKS, &report);
  CHECK_EQ(n, 1u);
  CHECK_STR_EQ(out[0].ssid, "My Home Network");
  CHECK_STR_EQ(out[0].password, "hunter2");
}

static void test_wifi_parses_crlf_line_endings() {
  const char* text =
      "NetworkA passwordA\r\nNetworkB passwordB\r\nNetworkC passwordC\r\n";
  infrasound::WifiCredential out[infrasound::WIFI_MAX_NETWORKS];
  infrasound::WifiParseReport report;
  size_t n = infrasound::wifiParseCredentials(
      text, out, infrasound::WIFI_MAX_NETWORKS, &report);
  CHECK_EQ(n, 3u);
  CHECK_STR_EQ(out[0].ssid, "NetworkA");
  CHECK_STR_EQ(out[0].password, "passwordA");
  CHECK_STR_EQ(out[1].ssid, "NetworkB");
  CHECK_STR_EQ(out[1].password, "passwordB");
  CHECK_STR_EQ(out[2].ssid, "NetworkC");
  CHECK_STR_EQ(out[2].password, "passwordC");
  // If '\r' leaked into the password (the single most likely real-world
  // failure), its length would be one longer than expected even though
  // CHECK_STR_EQ above might still pass on some libc strcmp behaviors.
  CHECK_EQ(std::strlen(out[0].password), 9u);
}

static void test_wifi_skips_blank_lines_and_trailing_newline() {
  const char* text = "\nNetworkA passwordA\n\n\nNetworkB passwordB\n\n";
  infrasound::WifiCredential out[infrasound::WIFI_MAX_NETWORKS];
  infrasound::WifiParseReport report;
  size_t n = infrasound::wifiParseCredentials(
      text, out, infrasound::WIFI_MAX_NETWORKS, &report);
  CHECK_EQ(n, 2u);
  CHECK_EQ(report.malformed, 0u);
  CHECK_STR_EQ(out[0].ssid, "NetworkA");
  CHECK_STR_EQ(out[1].ssid, "NetworkB");
}

static void test_wifi_malformed_line_is_skipped_and_counted() {
  const char* text = "NetworkA passwordA\nNoSpaceHere\nNetworkC passwordC\n";
  infrasound::WifiCredential out[infrasound::WIFI_MAX_NETWORKS];
  infrasound::WifiParseReport report;
  size_t n = infrasound::wifiParseCredentials(
      text, out, infrasound::WIFI_MAX_NETWORKS, &report);
  CHECK_EQ(n, 2u);
  CHECK_EQ(report.malformed, 1u);
  CHECK_STR_EQ(out[0].ssid, "NetworkA");
  CHECK_STR_EQ(out[1].ssid, "NetworkC");
}

static void test_wifi_legacy_two_line_file_is_detected() {
  const char* text = "MySSID\nMyPassword\n";
  infrasound::WifiCredential out[infrasound::WIFI_MAX_NETWORKS];
  infrasound::WifiParseReport report;
  size_t n = infrasound::wifiParseCredentials(
      text, out, infrasound::WIFI_MAX_NETWORKS, &report);
  CHECK_EQ(n, 1u);
  CHECK(report.legacy);
  CHECK_STR_EQ(out[0].ssid, "MySSID");
  CHECK_STR_EQ(out[0].password, "MyPassword");
}

static void test_wifi_two_line_new_format_is_not_legacy() {
  const char* text = "NetworkA passwordA\nNetworkB passwordB\n";
  infrasound::WifiCredential out[infrasound::WIFI_MAX_NETWORKS];
  infrasound::WifiParseReport report;
  size_t n = infrasound::wifiParseCredentials(
      text, out, infrasound::WIFI_MAX_NETWORKS, &report);
  CHECK_EQ(n, 2u);
  CHECK(!report.legacy);
  CHECK_STR_EQ(out[0].ssid, "NetworkA");
  CHECK_STR_EQ(out[1].ssid, "NetworkB");
}

static void test_wifi_two_line_file_only_first_has_space_is_not_legacy() {
  const char* text = "NetworkA passwordA\nNoSpaceHere\n";
  infrasound::WifiCredential out[infrasound::WIFI_MAX_NETWORKS];
  infrasound::WifiParseReport report;
  size_t n = infrasound::wifiParseCredentials(
      text, out, infrasound::WIFI_MAX_NETWORKS, &report);
  CHECK(!report.legacy);
  CHECK_EQ(n, 1u);
  CHECK_EQ(report.malformed, 1u);
  CHECK_STR_EQ(out[0].ssid, "NetworkA");
}

static void test_wifi_more_than_max_networks_are_counted_over_capacity() {
  const char* text =
      "Net0 pw0\nNet1 pw1\nNet2 pw2\nNet3 pw3\nNet4 pw4\nNet5 pw5\n"
      "Net6 pw6\nNet7 pw7\nNet8 pw8\nNet9 pw9\n";
  infrasound::WifiCredential out[infrasound::WIFI_MAX_NETWORKS];
  infrasound::WifiParseReport report;
  size_t n = infrasound::wifiParseCredentials(
      text, out, infrasound::WIFI_MAX_NETWORKS, &report);
  CHECK_EQ(n, infrasound::WIFI_MAX_NETWORKS);
  CHECK_EQ(report.parsed, infrasound::WIFI_MAX_NETWORKS);
  CHECK_EQ(report.over_capacity, 2u);
  CHECK_STR_EQ(out[0].ssid, "Net0");
  CHECK_STR_EQ(out[7].ssid, "Net7");
}

static void test_wifi_over_long_ssid_and_password_are_truncated() {
  // 40 'S' chars is over WIFI_MAX_SSID_LEN's capacity of 32 + NUL; 70 'P'
  // chars is over WIFI_MAX_PASS_LEN's capacity of 63 + NUL.
  char long_ssid[41];
  std::memset(long_ssid, 'S', 40);
  long_ssid[40] = '\0';
  char long_password[71];
  std::memset(long_password, 'P', 70);
  long_password[70] = '\0';

  char line[41 + 1 + 70 + 2];
  std::snprintf(line, sizeof(line), "%s %s\n", long_ssid, long_password);

  infrasound::WifiCredential out[infrasound::WIFI_MAX_NETWORKS];
  infrasound::WifiParseReport report;
  size_t n = infrasound::wifiParseCredentials(
      line, out, infrasound::WIFI_MAX_NETWORKS, &report);
  CHECK_EQ(n, 1u);
  CHECK_EQ(report.parsed, 1u);
  CHECK_EQ(std::strlen(out[0].ssid), infrasound::WIFI_MAX_SSID_LEN - 1);
  CHECK_EQ(std::strlen(out[0].password), infrasound::WIFI_MAX_PASS_LEN - 1);
  // Every retained character must actually be the source character - a
  // truncation bug that copies from the wrong offset would still pass a
  // pure length check.
  CHECK_EQ(out[0].ssid[0], 'S');
  CHECK_EQ(out[0].password[0], 'P');
}

int main() {
  test_harness_works();
  test_crc8_matches_sdp600_algorithm();
  test_encode_frame_layout();
  test_encode_frame_payload_can_contain_marker_bytes();
  test_decoder_accepts_a_clean_frame();
  test_decoder_rejects_corrupted_crc();
  test_corrupted_frame_does_not_swallow_the_next_valid_one();
  test_decoder_resyncs_after_arbitrary_garbage();
  test_decoder_rejects_implausible_timestamp();
  test_first_frame_is_accepted_without_a_reference();
  test_arm_reseed_accepts_a_discontinuous_timestamp();
  test_plausibility_survives_the_millis_wrap();
  test_header_round_trip();
  test_header_reserved_bytes_are_zero();
  test_parse_header_rejects_missing_magic();
  test_record_round_trip();
  test_record_count_is_derived_from_file_size();
  test_legacy_file_record_count_and_timestamps();
  test_csv_header_is_exactly_twenty_bytes();
  test_csv_line_is_exactly_twenty_four_bytes_across_the_range();
  test_csv_line_exact_content();
  test_csv_line_pads_small_timestamps_to_thirteen_digits();
  test_csv_line_handles_non_finite_at_fixed_width();
  test_csv_line_clamps_out_of_range_values();
  test_output_offset_maps_to_record_index();
  test_sd_failure_is_solid_on();
  test_no_time_blinks_at_five_hertz();
  test_ok_is_a_short_flash_every_two_seconds();
  test_not_logging_is_a_double_blink();
  test_no_data_is_a_triple_flash();
  test_stopped_is_a_slow_even_blink();
  test_patterns_are_distinguishable_by_duty_cycle();
  test_all_six_states_are_distinguishable();
  test_diagnostics_all_clear();
  test_format_diagnostics_includes_every_counter();
  test_format_diagnostics_respects_a_small_buffer();
  test_manifest_entry_round_trip();
  test_manifest_rejects_an_unterminated_path();
  test_needs_copy_detects_changes();
  test_crc32_is_streamable_and_matches_known_vector();
  test_manifest_entry_raw_byte_layout();
  test_path_fits();

  test_wifi_parses_normal_multiline_file_in_order();
  test_wifi_ssid_can_contain_spaces();
  test_wifi_parses_crlf_line_endings();
  test_wifi_skips_blank_lines_and_trailing_newline();
  test_wifi_malformed_line_is_skipped_and_counted();
  test_wifi_legacy_two_line_file_is_detected();
  test_wifi_two_line_new_format_is_not_legacy();
  test_wifi_two_line_file_only_first_has_space_is_not_legacy();
  test_wifi_more_than_max_networks_are_counted_over_capacity();
  test_wifi_over_long_ssid_and_password_are_truncated();

  std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
