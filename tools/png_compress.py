#!/usr/bin/env python3
"""Recompresses PNG files in place (the simulator writes them uncompressed). Standard library only.

    python3 tools/png_compress.py docs/images/*.png
"""
import struct
import sys
import zlib


def chunks(data):
    pos = 8
    while pos < len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        yield kind, data[pos + 8:pos + 8 + n]
        pos += 12 + n


def chunk(kind, body):
    return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)


def recompress(path):
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit(f"{path}: not a PNG")
    out, idat = [data[:8]], b""
    for kind, body in chunks(data):
        if kind == b"IDAT":
            idat += body
        elif kind == b"IEND":
            out.append(chunk(b"IDAT", zlib.compress(zlib.decompress(idat), 9)))
            out.append(chunk(b"IEND", b""))
        else:
            out.append(chunk(kind, body))
    new = b"".join(out)
    if len(new) < len(data):
        open(path, "wb").write(new)
    print(f"{path}: {len(data) // 1024} KB -> {len(new) // 1024} KB")


for p in sys.argv[1:]:
    recompress(p)
