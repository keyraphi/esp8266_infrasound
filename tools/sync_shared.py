"""Copies shared headers from their canonical location into each sketch folder.

Run after editing any canonical shared header. tests/test_shared_sync.py
enforces that this has been done.
"""
import pathlib
import shutil
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent

PAIRS = [
    (
        ROOT / "esp8266_infrasound_webserver" / "infrasound_frame.h",
        ROOT / "arduino_infrasound_sensor" / "infrasound_frame.h",
    ),
]


def main() -> int:
    for canonical, copy in PAIRS:
        if not canonical.exists():
            print(f"ERROR canonical file missing: {canonical}")
            return 1
        copy.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(canonical, copy)
        print(f"synced {canonical.relative_to(ROOT)} -> {copy.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
