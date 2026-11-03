#!/usr/bin/env python3
"""Predicts how M5Launcher (github.com/bmorcelli/Launcher) installs a merged firmware .bin from the
SD card: which app size it copies and how big it makes each data partition.
Mirrors updateFromSD() in Launcher/src/sd_functions.cpp (Cardputer: SPIFFS threshold 0x500000).

    python3 tools/launcher_check.py firmware-merged.bin [--strict]

--strict: exit code 1 if the LittleFS/SPIFFS data partition would be shrunk to 448 KB.
"""
import struct
import sys

THRESHOLD = 0x500000        # LAUNCHER_DEFAULT_SPIFFS_THRESHOLD (Cardputer)
DEFAULT_SPIFFS = 0x70000    # LAUNCHER_DEFAULT_SPIFFS_SIZE


def main():
    path = sys.argv[1]
    strict = "--strict" in sys.argv
    d = open(path, "rb").read()
    if d[0x8000:0x8003] != b"\xaa\x50\x01":
        print("no partition table at 0x8000: Launcher installs the app alone, WITHOUT a data partition")
        return 1 if strict else 0
    ok = True
    app_done = False
    for i in range(0x8000, 0x8C00, 32):
        e = d[i:i + 32]
        if e[:2] in (b"\xff\xff", b"\xeb\xeb"):
            break
        t, st = e[2], e[3]
        off, size = struct.unpack("<II", e[4:12])
        label = e[12:28].split(b"\0")[0].decode() or "spiffs"
        if t == 0 and st in (0x00, 0x10, 0x20) and not app_done:
            app = len(d) - off if len(d) < off + size else size
            print(f"app   {label:9s} copies {app // 1024} KB")
            app_done = True
        if t == 1 and st in (0x82, 0x83):
            empty = len(d) <= off or not any(b != 0xFF for b in d[off:off + 4096])
            if empty and size <= THRESHOLD:
                result, note = DEFAULT_SPIFFS, "SHRUNK to 448 KB"
                ok = False
            elif label != "spiffs" and size > DEFAULT_SPIFFS:
                result, note = size, "kept as declared"
            elif size > THRESHOLD:
                result, note = None, "fills the remaining free flash"
            else:
                result, note = DEFAULT_SPIFFS, "SHRUNK to 448 KB"
                ok = False
            shown = f"{result // 1024} KB" if result else "all free space"
            print(f"data  {label:9s} declared {size // 1024} KB -> {shown} ({note})")
    print("Launcher compatibility:", "OK" if ok else "reduced storage")
    return 0 if ok or not strict else 1


if __name__ == "__main__":
    sys.exit(main())
