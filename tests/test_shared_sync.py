"""Fails if the duplicated shared headers have drifted apart.

The Arduino IDE cannot include headers from outside the sketch folder, so the
frame protocol header exists in both sketches. This test enforces that the
copies stay byte-identical; run tools/sync_shared.py to fix a failure.
"""
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent

PAIRS = [
    (
        ROOT / "esp8266_infrasound_webserver" / "infrasound_frame.h",
        ROOT / "arduino_infrasound_sensor" / "infrasound_frame.h",
    ),
]


def main() -> int:
    failures = 0
    for canonical, copy in PAIRS:
        if not canonical.exists():
            print(f"FAIL missing canonical file: {canonical}")
            failures += 1
            continue
        if not copy.exists():
            print(f"FAIL missing copy: {copy} (run tools/sync_shared.py)")
            failures += 1
            continue
        if canonical.read_bytes() != copy.read_bytes():
            print(f"FAIL drift: {copy} differs from {canonical}")
            print("     run: python tools/sync_shared.py")
            failures += 1
        else:
            print(f"ok   {copy.relative_to(ROOT)}")
    print(f"\n{len(PAIRS)} pairs checked, {failures} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
