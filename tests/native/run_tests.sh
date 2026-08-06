#!/usr/bin/env bash
set -euo pipefail

# g++ is installed but not on PATH in this environment.
for candidate in \
  /c/ProgramData/mingw64/mingw64/bin \
  /c/msys64/mingw64/bin \
  /c/mingw64/bin
do
  if [ -x "$candidate/g++.exe" ]; then
    export PATH="$candidate:$PATH"
    break
  fi
done

if ! command -v g++ >/dev/null 2>&1; then
  echo "ERROR: g++ not found. Install MinGW-w64 or add it to PATH." >&2
  exit 127
fi

cd "$(dirname "$0")"
mkdir -p build

g++ -std=gnu++17 -Wall -Wextra -Werror \
    -I../../esp8266_infrasound_webserver \
    -o build/tests.exe test_main.cpp

./build/tests.exe
