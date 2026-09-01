// Manifest describing which static assets have been copied SD -> LittleFS.
//
// Change detection uses size + FAT modification time, which needs only a
// directory read. Hashing the whole tree would mean reading 816 KB off the SD
// on every boot purely to conclude nothing changed. The CRC32 is computed
// while streaming the bytes during a copy, so it costs nothing, and it
// verifies the copy landed intact.
//
// Must not include Arduino.h — this header is unit tested natively.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace infrasound {

constexpr size_t SM_MAX_PATH   = 48;
constexpr size_t SM_ENTRY_SIZE = 64;

struct ManifestEntry {
  char     path[SM_MAX_PATH];
  uint32_t size;
  uint16_t fat_date;
  uint16_t fat_time;
  uint32_t crc32;
};

inline void smPackEntry(const ManifestEntry& e, uint8_t out[SM_ENTRY_SIZE]) {
  std::memset(out, 0, SM_ENTRY_SIZE);
  const size_t path_len = std::strlen(e.path);
  const size_t copy_len = path_len < SM_MAX_PATH ? path_len : SM_MAX_PATH - 1;
  std::memcpy(out + 0, e.path, copy_len);
  // bytes copy_len..47 stay zero from the memset above, so the on-disk entry
  // is deterministic regardless of how the caller built the struct
  std::memcpy(out + 48, &e.size, 4);
  std::memcpy(out + 52, &e.fat_date, 2);
  std::memcpy(out + 54, &e.fat_time, 2);
  std::memcpy(out + 56, &e.crc32, 4);
  // bytes 60..63 reserved, zero
}

// True when `path` fits an entry with room for its terminator. smPackEntry
// truncates silently, which would make two long paths collide on one entry,
// so callers must reject a path this rejects rather than pack it.
inline bool smPathFits(const char* path) {
  return std::strlen(path) < SM_MAX_PATH;
}

inline bool smParseEntry(const uint8_t in[SM_ENTRY_SIZE], ManifestEntry* out) {
  if (std::memchr(in, '\0', SM_MAX_PATH) == nullptr) return false;
  std::memcpy(out->path, in + 0, SM_MAX_PATH);
  std::memcpy(&out->size, in + 48, 4);
  std::memcpy(&out->fat_date, in + 52, 2);
  std::memcpy(&out->fat_time, in + 54, 2);
  std::memcpy(&out->crc32, in + 56, 4);
  return true;
}

inline bool smNeedsCopy(const ManifestEntry& stored, uint32_t size,
                        uint16_t fat_date, uint16_t fat_time) {
  return stored.size != size || stored.fat_date != fat_date ||
         stored.fat_time != fat_time;
}

// Standard CRC-32 (reflected, polynomial 0xEDB88320). Pass the previous return
// value as `seed` to continue a stream; pass 0 to start.
inline uint32_t smCrc32(const uint8_t* data, size_t len, uint32_t seed) {
  uint32_t crc = ~seed;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xEDB88320u & (~(crc & 1u) + 1u));
    }
  }
  return ~crc;
}

}  // namespace infrasound
