#!/usr/bin/env python3
"""Builds firmware-dual.bin: standalone keyboard firmware + MeshCore phone companion in one image.

    pio run -e cardputer_adv_dual_standalone -e cardputer_adv_dual_companion
    python3 tools/make_dual.py                      # -> .pio/build/firmware-dual.bin

Flash it at 0x0 (M5Burner, web flasher or esptool write_flash 0x0 firmware-dual.bin).
Layout: variant/partitions_cpadv_dual.csv. Pure Python, no esptool needed.
"""
import argparse
import csv
import io
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
CSV = os.path.join(ROOT, "variant", "partitions_cpadv_dual.csv")

BOOTLOADER_OFFSET = 0x0       # ESP32-S3
PARTITION_TABLE_OFFSET = 0x8000


def parse_csv(path):
    parts = {}
    with open(path) as f:
        lines = [l for l in f if l.strip() and not l.lstrip().startswith("#")]
    for row in csv.reader(io.StringIO("".join(lines))):
        row = [c.strip() for c in row]
        name, _type, sub, off, size = row[:5]
        parts[name] = (int(off, 0), int(size, 0), sub)
    return parts


def read(path):
    with open(path, "rb") as f:
        return f.read()


def app_version(img):
    """Version, date and time from the app descriptor (offset 0x20 of an ESP32 app image), or None.
    Written by tools/pio_version.py; older builds carry the Arduino framework's own descriptor."""
    if len(img) < 0x20 + 112 or img[0x20:0x24] != bytes.fromhex("3254cdab"):
        return None
    field = lambda off, n: img[0x20 + off:0x20 + off + n].split(b"\0")[0].decode(errors="replace")
    return field(16, 32), field(96, 16), field(80, 16)


def find_boot_app0():
    candidates = [
        os.path.expanduser("~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"),
    ]
    for c in candidates:
        if os.path.exists(c):
            return c
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build-dir", default=os.path.join(ROOT, ".pio", "build"))
    ap.add_argument("--standalone-env", default="cardputer_adv_dual_standalone")
    ap.add_argument("--companion-env", default="cardputer_adv_dual_companion")
    ap.add_argument("-o", "--output", default=None)
    args = ap.parse_args()

    parts = parse_csv(CSV)
    app0_off, app0_size, _ = parts["app0"]
    app1_off, app1_size, _ = parts["app1"]
    ota_off, ota_size, _ = parts["otadata"]

    sdir = os.path.join(args.build_dir, args.standalone_env)
    cdir = os.path.join(args.build_dir, args.companion_env)
    files = {
        "bootloader": os.path.join(sdir, "bootloader.bin"),
        "partitions": os.path.join(sdir, "partitions.bin"),
        "standalone": os.path.join(sdir, "firmware.bin"),
        "companion": os.path.join(cdir, "firmware.bin"),
    }
    for k, p in files.items():
        if not os.path.exists(p):
            sys.exit(f"missing {k}: {p}\n  build first: pio run -e {args.standalone_env} -e {args.companion_env}")

    errors = []
    part_c = os.path.join(cdir, "partitions.bin")
    if os.path.exists(part_c) and read(part_c) != read(files["partitions"]):
        errors.append("the two builds use different partition tables (both envs must use partitions_cpadv_dual.csv)")

    boot = read(files["bootloader"])
    table = read(files["partitions"])
    app0 = read(files["standalone"])
    app1 = read(files["companion"])
    for name, img in (("bootloader", boot), ("standalone", app0), ("companion", app1)):
        if not img or img[0] != 0xE9:
            errors.append(f"{name}: not an ESP32 image (magic 0x{img[0] if img else 0:02X})")
    if len(app0) > app0_size:
        errors.append(f"standalone firmware {len(app0)} B > app0 {app0_size} B")
    if len(app1) > app1_size:
        errors.append(f"companion firmware {len(app1)} B > app1 {app1_size} B")
    if errors:
        sys.exit("ERROR:\n  " + "\n  ".join(errors))

    end = app1_off + len(app1)
    out = bytearray(b"\xff" * end)

    def put(off, data, what):
        out[off:off + len(data)] = data
        print(f"  0x{off:06X}  {len(data):>8} B  {what}")

    print("firmware-dual.bin layout:")
    put(BOOTLOADER_OFFSET, boot, "bootloader")
    put(PARTITION_TABLE_OFFSET, table, "partition table (partitions_cpadv_dual.csv)")
    b0 = find_boot_app0()
    if b0:
        put(ota_off, read(b0)[:ota_size], "otadata (boot app0 = standalone)")
    else:
        print(f"  0x{ota_off:06X}  (blank otadata: boots app0)")
    v0, v1 = app_version(app0), app_version(app1)
    def label(v):
        return f"{v[0]}, built {v[1]} {v[2]}" if v else "version unknown"
    put(app0_off, app0, f"app0 standalone  ({100 * len(app0) // app0_size}% of {app0_size // 1024} KB) {label(v0)}")
    put(app1_off, app1, f"app1 companion   ({100 * len(app1) // app1_size}% of {app1_size // 1024} KB) {label(v1)}")
    if not v0 or not v1 or v0[0] != v1[0]:
        print(f"WARNING: the two slots come from different builds ({v0[0] if v0 else '?'} / {v1[0] if v1 else '?'}):"
              f" rebuild both envs before flashing")

    output = args.output or os.path.join(args.build_dir, "firmware-dual.bin")
    with open(output, "wb") as f:
        f.write(out)
    print(f"written {output} ({len(out)} B). Flash at 0x0. LittleFS data starts at 0x{parts['spiffs'][0]:06X}.")


if __name__ == "__main__":
    main()
