// Copies /www/static from the SD card into LittleFS at boot, so the HTTP
// server never touches the SD card to serve the web UI.
//
// Detection is size + FAT modification time, read from directory entries only.
// A CRC32 is computed while streaming the bytes during a copy, which costs
// nothing and verifies the copy landed intact. Files are written to a temp
// name and renamed; LittleFS renames are atomic, so power loss during a copy
// leaves the previous version rather than a truncated one.
#pragma once

bool staticSyncRun();
