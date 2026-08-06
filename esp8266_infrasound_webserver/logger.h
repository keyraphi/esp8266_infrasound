// SD writer for measurement files.
//
// The file is held open for the life of a part file. Flushes are batched to
// 512 bytes, matching SdFat's cache block size — but this is not physical
// sector alignment: the file header offsets every record by 32 bytes, so
// flushes do not land on sector boundaries (see the RECORDS_PER_FLUSH
// comment in logger.cpp). sync() is decoupled on a 10 s timer because sync()
// is what touches the directory entry and FAT — the expensive,
// variable-latency part.
#pragma once

#include <Arduino.h>
#include <SdFat.h>

#include <cstdint>

// Wall-clock time at t_rel_ms == 0, i.e. the instant of the first accepted
// sensor frame -- NOT device boot. The gap between them is SD init, WiFi
// association and NTP, which is seconds. Returns 0 when there is no NTP,
// which downstream renders as relative time.
//
// This lives in exactly one place on purpose: five separate bugs in this
// project came from computing it inline and getting the origin wrong. The
// file header and the live view's /start_timestamp MUST agree, or a recording
// and the chart above it disagree about what time it is.
uint64_t loggerEpoch0Ms();

// Arms logging for this run but does not open a part file yet -- see
// loggerAppend(). Returns false only if /measurements could not be created.
bool loggerBegin(uint32_t run_number);
// Opens the first part file on its first call after loggerBegin() (or after
// loggerStartNewPart()), so the header is written from the start_timestamp
// and service_millis_at_t0 of the actual first sample, not of setup().
void loggerAppend(uint32_t t_rel_ms, float v);
void loggerTick(uint32_t now_ms);
// No-op if no part has been opened yet (loggerBegin() armed logging but no
// sample has arrived): there is nothing to rotate away from, and forcing an
// open here would burn a part number on a possibly-empty file.
void loggerStartNewPart();
void loggerStop();
// True once loggerBegin() has armed logging, even before a part file has
// physically been opened -- see logger.cpp.
bool loggerIsLogging();
const String& loggerFileName();
uint32_t loggerRunNumber();
uint8_t loggerPartNumber();

// Reads /measurements/.run, increments it, writes it back, and returns the new
// value. Written before the first measurement file is created so a crash can
// never reuse a number. Returns 1 on a fresh or unreadable card.
uint32_t readAndIncrementRunCounter();
