#!/usr/bin/env python3
"""Offline map tiles for the Cardputer ADV standalone firmware.

Converts a zone of raster map tiles (256x256 PNG/JPEG) into raw big-endian RGB565 files that the
Cardputer reads row by row straight from the SD card, without decoding:

    <out>/tiles/<z>/<x>/<y>.bin     131072 bytes each
    <out>/tiles/info.txt            "lat lon zmin zmax" (default view)

Copy the <out>/tiles folder to the root of the SD card (FAT32).

Sources (pick one):
  --url TEMPLATE     a tile server you are allowed to download from for offline use,
                     e.g. "https://tiles.example.org/{z}/{x}/{y}.png". The public
                     tile.openstreetmap.org servers do NOT allow bulk downloads (see
                     https://operations.osmfoundation.org/policies/tiles/): use a provider whose
                     terms permit it, or your own tile server.
  --mbtiles FILE     a raster .mbtiles file (e.g. exported from QGIS / MOBAC).
  --synthetic        generated test pattern (no download, no Pillow needed).

Examples:
  python3 make_tiles.py --center 43.6116 1.3346 --radius-km 10 --zooms 12-15 \\
          --url "https://tiles.example.org/{z}/{x}/{y}.png" --out carte_colomiers
  python3 make_tiles.py --bbox 43.52 1.24 43.70 1.43 --zooms 11-14 --mbtiles toulouse.mbtiles --out sd
"""
import argparse
import math
import os
import sqlite3
import struct
import sys
import time
import urllib.request

TILE = 256


def deg2tile(lat, lon, z):
    lat = max(min(lat, 85.0511), -85.0511)
    n = 2 ** z
    x = (lon + 180.0) / 360.0 * n
    s = math.sin(math.radians(lat))
    y = (0.5 - math.log((1 + s) / (1 - s)) / (4 * math.pi)) * n
    return x, y


def tile_range(south, west, north, east, z):
    x0, y0 = deg2tile(north, west, z)
    x1, y1 = deg2tile(south, east, z)
    return int(x0), int(y0), int(x1), int(y1)


def rgb_to_565be(rgb_bytes):
    out = bytearray(len(rgb_bytes) // 3 * 2)
    j = 0
    for i in range(0, len(rgb_bytes), 3):
        r, g, b = rgb_bytes[i], rgb_bytes[i + 1], rgb_bytes[i + 2]
        v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        out[j] = v >> 8
        out[j + 1] = v & 0xFF
        j += 2
    return bytes(out)


def image_to_565(data):
    try:
        from PIL import Image
    except ImportError:
        sys.exit("Pillow is required to convert PNG/JPEG tiles: pip install pillow")
    import io
    img = Image.open(io.BytesIO(data)).convert("RGB")
    if img.size != (TILE, TILE):
        img = img.resize((TILE, TILE))
    return rgb_to_565be(img.tobytes())


def synthetic_tile(z, x, y):
    """Readable test pattern: land, a river, a road grid, parks. Pure Python."""
    land, road, water, park, label = (238, 232, 220), (255, 255, 255), (150, 190, 230), (190, 225, 180), (200, 120, 90)
    px0, py0 = x * TILE, y * TILE
    scale = 2 ** (z - 13)   # features keep their ground size across zoom levels
    rows = []
    for r in range(TILE):
        gy = (py0 + r) / scale
        row = bytearray()
        for c in range(TILE):
            gx = (px0 + c) / scale
            col = land
            if int(gx) % 400 < 60 and int(gy) % 300 < 50:
                col = park
            if abs(gy - (764000 + 60 * math.sin(gx / 90.0))) < 9:
                col = water
            if int(gx) % 200 < 3 or int(gy) % 170 < 3:
                col = road
            if c < 1 or r < 1:
                col = label   # tile border, helps checking alignment
            row += bytes(col)
        rows.append(bytes(row))
    return rgb_to_565be(b"".join(rows))


def open_mbtiles(path, zmin, zmax):
    """Opens an existing raster .mbtiles read-only, with clear errors (never creates a file)."""
    if not os.path.isfile(path):
        sys.exit(f"error: {path} not found. --mbtiles needs an existing .mbtiles file "
                 "(export one from QGIS or MOBAC), or use --synthetic to test.")
    try:
        db = sqlite3.connect(f"file:{os.path.abspath(path)}?mode=ro", uri=True)
        names = {r[0] for r in db.execute("SELECT name FROM sqlite_master WHERE type IN ('table','view')")}
    except sqlite3.DatabaseError as e:
        sys.exit(f"error: {path} is not a valid .mbtiles file ({e})")
    if "tiles" not in names:
        sys.exit(f"error: {path} has no 'tiles' table: empty or not an MBTiles file "
                 "(delete it if it was created by mistake)")
    meta = {}
    if "metadata" in names:
        meta = dict(db.execute("SELECT name, value FROM metadata").fetchall())
    fmt = meta.get("format", "?")
    if fmt in ("pbf", "mvt"):
        sys.exit(f"error: {path} contains vector tiles ({fmt}); a raster .mbtiles (png/jpg) is needed")
    lo, hi = db.execute("SELECT MIN(zoom_level), MAX(zoom_level) FROM tiles").fetchone()
    print(f"{path}: format {fmt}, zoom {lo}-{hi}" + (f", '{meta['name']}'" if "name" in meta else ""))
    if lo is None or zmax < lo or zmin > hi:
        sys.exit(f"error: the file has no tile in zoom {zmin}-{zmax}")
    return db


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    zone = ap.add_mutually_exclusive_group(required=True)
    zone.add_argument("--center", nargs=2, type=float, metavar=("LAT", "LON"))
    zone.add_argument("--bbox", nargs=4, type=float, metavar=("SOUTH", "WEST", "NORTH", "EAST"))
    ap.add_argument("--radius-km", type=float, default=10.0, help="with --center (default 10)")
    ap.add_argument("--zooms", default="12-15", help="e.g. 12-15 or 13 (default 12-15)")
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--url")
    src.add_argument("--mbtiles")
    src.add_argument("--synthetic", action="store_true")
    ap.add_argument("--out", required=True, help="output folder (contains tiles/)")
    ap.add_argument("--user-agent", default="cardputer-meshcore-tiles/1.0 (personal offline use)")
    ap.add_argument("--delay", type=float, default=0.5, help="seconds between downloads (default 0.5)")
    ap.add_argument("--max-tiles", type=int, default=5000, help="safety limit (default 5000)")
    a = ap.parse_args()

    if "-" in a.zooms:
        zmin, zmax = (int(v) for v in a.zooms.split("-"))
    else:
        zmin = zmax = int(a.zooms)
    if a.center:
        lat, lon = a.center
        dlat = a.radius_km / 111.32
        dlon = a.radius_km / (111.32 * math.cos(math.radians(lat)))
        south, west, north, east = lat - dlat, lon - dlon, lat + dlat, lon + dlon
    else:
        south, west, north, east = a.bbox
        lat, lon = (south + north) / 2, (west + east) / 2

    jobs = []
    for z in range(zmin, zmax + 1):
        x0, y0, x1, y1 = tile_range(south, west, north, east, z)
        jobs += [(z, x, y) for x in range(x0, x1 + 1) for y in range(y0, y1 + 1)]
    size_mb = len(jobs) * TILE * TILE * 2 / 1e6
    print(f"{len(jobs)} tiles, zoom {zmin}-{zmax}, {size_mb:.1f} MB on the SD card")
    if len(jobs) > a.max_tiles:
        sys.exit(f"more than --max-tiles={a.max_tiles}: reduce the zone or the zoom levels")

    db = open_mbtiles(a.mbtiles, zmin, zmax) if a.mbtiles else None
    base = os.path.join(a.out, "tiles")
    done = skipped = missing = 0
    for z, x, y in jobs:
        path = os.path.join(base, str(z), str(x), f"{y}.bin")
        if os.path.exists(path) and os.path.getsize(path) == TILE * TILE * 2:
            skipped += 1
            continue
        if a.synthetic:
            raw = synthetic_tile(z, x, y)
        elif db:
            row = db.execute("SELECT tile_data FROM tiles WHERE zoom_level=? AND tile_column=? AND tile_row=?",
                             (z, x, (2 ** z - 1) - y)).fetchone()   # MBTiles rows are TMS (flipped)
            if not row:
                missing += 1
                continue
            raw = image_to_565(row[0])
        else:
            req = urllib.request.Request(a.url.format(z=z, x=x, y=y), headers={"User-Agent": a.user_agent})
            with urllib.request.urlopen(req, timeout=30) as r:
                raw = image_to_565(r.read())
            time.sleep(a.delay)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as f:
            f.write(raw)
        done += 1
        if done % 25 == 0:
            print(f"  {done}/{len(jobs)}")
    os.makedirs(base, exist_ok=True)
    with open(os.path.join(base, "info.txt"), "w") as f:
        f.write(f"{lat:.6f} {lon:.6f} {zmin} {zmax}\n")
    if missing:
        print(f"warning: {missing} tiles of the zone are not in {a.mbtiles} (outside its area or zoom levels)")
    print(f"done: {done} written, {skipped} already there -> copy '{base}' to the SD card root")


if __name__ == "__main__":
    main()
