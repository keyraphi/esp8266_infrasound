#include "static_sync.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <SdFat.h>
#include <cstring>

#include "static_manifest.h"

extern SdFs sd;

namespace {

constexpr const char* kSdRoot        = "/www/static";
constexpr const char* kFsRoot        = "/static";
constexpr const char* kManifestPath     = "/static.manifest";
constexpr const char* kManifestTempPath = "/static.manifest.tmp";
constexpr const char* kTempPath         = "/static.tmp";
constexpr size_t      kCopyChunk     = 512;
constexpr size_t      kMaxEntries    = 32;

infrasound::ManifestEntry g_manifest[kMaxEntries];
size_t g_manifest_count = 0;

void loadManifest() {
  g_manifest_count = 0;
  File f = LittleFS.open(kManifestPath, "r");
  if (!f) return;
  uint8_t buf[infrasound::SM_ENTRY_SIZE];
  while (g_manifest_count < kMaxEntries &&
         f.read(buf, sizeof(buf)) == static_cast<int>(sizeof(buf))) {
    if (infrasound::smParseEntry(buf, &g_manifest[g_manifest_count])) {
      ++g_manifest_count;
    }
  }
  f.close();
}

// Every asset this manifest tracks is written temp-then-rename so a crash
// mid-write leaves the previous version rather than a truncated one; the
// manifest itself must get the same treatment; a crash mid-truncate-write
// of the real path would otherwise garble the tracking state and force a
// full re-copy of everything on the next boot.
void saveManifest() {
  File f = LittleFS.open(kManifestTempPath, "w");
  if (!f) return;
  uint8_t buf[infrasound::SM_ENTRY_SIZE];
  for (size_t i = 0; i < g_manifest_count; ++i) {
    infrasound::smPackEntry(g_manifest[i], buf);
    if (f.write(buf, sizeof(buf)) != sizeof(buf)) {
      // A short write means the temp manifest is incomplete. Renaming it over
      // the real one would atomically replace good tracking state with bad, so
      // bail out and leave the previous manifest untouched — the worst case is
      // then a re-copy on the next boot, not corrupted state.
      Serial.println("Failed to write static asset manifest, keeping previous");
      f.close();
      LittleFS.remove(kManifestTempPath);
      return;
    }
  }
  f.close();

  if (!LittleFS.rename(kManifestTempPath, kManifestPath)) {
    LittleFS.remove(kManifestPath);
    if (!LittleFS.rename(kManifestTempPath, kManifestPath)) {
      LittleFS.remove(kManifestTempPath);
      Serial.println("Failed to save static asset manifest; will re-scan next boot");
    }
  }
}

infrasound::ManifestEntry* findEntry(const char* path) {
  for (size_t i = 0; i < g_manifest_count; ++i) {
    if (strcmp(g_manifest[i].path, path) == 0) return &g_manifest[i];
  }
  return nullptr;
}

void ensureParentDirs(const String& fs_path) {
  int slash = fs_path.indexOf('/', 1);
  while (slash > 0) {
    const String dir = fs_path.substring(0, slash);
    if (!LittleFS.exists(dir)) LittleFS.mkdir(dir);
    slash = fs_path.indexOf('/', slash + 1);
  }
}

// Returns true on success; sets crc_out to the CRC32 of the copied bytes.
// expected_size is the size recorded in the SD directory entry; a copy that
// writes a different number of bytes (e.g. src.read() returning a mid-stream
// I/O error) is treated as a failure rather than recorded as a successful,
// silently truncated copy.
bool copyFile(FsFile& src, const String& fs_path, uint32_t expected_size,
              uint32_t* crc_out) {
  ensureParentDirs(fs_path);

  File dst = LittleFS.open(kTempPath, "w");
  if (!dst) return false;

  uint8_t chunk[kCopyChunk];
  uint32_t crc = 0;
  uint32_t total_written = 0;
  int n;
  while ((n = src.read(chunk, sizeof(chunk))) > 0) {
    if (dst.write(chunk, n) != static_cast<size_t>(n)) {
      dst.close();
      LittleFS.remove(kTempPath);
      return false;
    }
    crc = infrasound::smCrc32(chunk, static_cast<size_t>(n), crc);
    total_written += static_cast<uint32_t>(n);
    yield();  // 816 KB copied without yielding would starve WiFi and trip the watchdog
  }
  dst.close();

  // n < 0 is an SD read error, not EOF; n == 0 with total_written short of
  // expected_size is the same failure by another path. Either way this is a
  // truncated copy and must not be recorded as a success, or the manifest
  // would mark the truncated file as matching its source forever.
  if (n < 0 || total_written != expected_size) {
    LittleFS.remove(kTempPath);
    return false;
  }

  // littlefs renames atomically replace an existing destination, which is the
  // whole point of the temp-then-rename idiom: try that first. Removing the
  // target before renaming would open a window where power loss leaves NO
  // file at all, which is worse than the stale file it replaces. Only fall
  // back to remove-then-rename if this filesystem layer refuses to overwrite
  // in place, since never updating an asset would be worse still.
  if (!LittleFS.rename(kTempPath, fs_path)) {
    LittleFS.remove(fs_path);
    if (!LittleFS.rename(kTempPath, fs_path)) {
      LittleFS.remove(kTempPath);
      return false;
    }
  }

  *crc_out = crc;
  return true;
}

bool syncDirectory(const String& sd_dir, const String& fs_dir, size_t* copied) {
  FsFile dir;
  if (!dir.open(sd_dir.c_str(), O_RDONLY)) return false;
  dir.rewind();

  FsFile entry;
  char name[64];
  while (entry.openNext(&dir, O_RDONLY)) {
    if (entry.isHidden()) { entry.close(); continue; }
    entry.getName(name, sizeof(name));

    const String sd_path = sd_dir + "/" + name;
    const String fs_path = fs_dir + "/" + name;

    if (entry.isDir()) {
      entry.close();
      if (!syncDirectory(sd_path, fs_path, copied)) {
        Serial.print("Failed to sync static asset subdirectory: ");
        Serial.println(sd_path);
      }
      continue;
    }

    // smPackEntry truncates any path of SM_MAX_PATH (48) characters or more,
    // which would make two long asset paths collide on one manifest entry and
    // leave one of them never updated again. Refuse to pack (and therefore to
    // copy-and-track) anything that doesn't fit, rather than risk that.
    if (!infrasound::smPathFits(fs_path.c_str())) {
      Serial.print("Static asset path too long for manifest, skipping: ");
      Serial.println(fs_path);
      entry.close();
      continue;
    }

    uint16_t fat_date = 0, fat_time = 0;
    entry.getModifyDateTime(&fat_date, &fat_time);
    const uint32_t size = static_cast<uint32_t>(entry.fileSize());

    infrasound::ManifestEntry* stored = findEntry(fs_path.c_str());
    if (stored != nullptr && LittleFS.exists(fs_path) &&
        !infrasound::smNeedsCopy(*stored, size, fat_date, fat_time)) {
      entry.close();
      continue;  // fast path: unchanged
    }

    uint32_t crc = 0;
    if (copyFile(entry, fs_path, size, &crc)) {
      infrasound::ManifestEntry* slot = stored;
      if (slot == nullptr && g_manifest_count < kMaxEntries) {
        slot = &g_manifest[g_manifest_count++];
      }
      if (slot != nullptr) {
        snprintf(slot->path, sizeof(slot->path), "%s", fs_path.c_str());
        slot->size     = size;
        slot->fat_date = fat_date;
        slot->fat_time = fat_time;
        slot->crc32    = crc;
      } else {
        // Manifest is full: the file was copied but cannot be tracked, so it
        // will be re-copied (and re-fail to be tracked) on every future boot
        // until the cap is raised. Silence here would hide the reason.
        Serial.print("Static manifest full, cannot track: ");
        Serial.println(fs_path);
      }
      ++(*copied);
    }
    entry.close();
  }
  dir.close();
  return true;
}

}  // namespace

bool staticSyncRun() {
  if (!LittleFS.begin()) {
    Serial.println("LittleFS mount failed; static assets unavailable");
    return false;
  }

  loadManifest();

  if (!sd.exists(kSdRoot)) {
    // No source tree on the card: keep serving whatever is already in flash.
    Serial.println("No /www/static on SD; serving static assets from flash");
    return true;
  }

  size_t copied = 0;
  if (!syncDirectory(kSdRoot, kFsRoot, &copied)) {
    Serial.println("Static sync failed; serving whatever is in flash");
    return false;
  }

  if (copied > 0) saveManifest();
  Serial.print("Static sync complete, files copied: ");
  Serial.println(copied);
  return true;
}
