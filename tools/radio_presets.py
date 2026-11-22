#!/usr/bin/env python3
"""Compares app/core/RadioPresets.h with MeshCore's own app presets (https://api.meshcore.nz/api/v1/config,
see meshcore/docs/radio_presets.md).

    python3 tools/radio_presets.py           # list both, side by side
    python3 tools/radio_presets.py --check   # exit 1 when the API has a preset the firmware lacks (in the Cap's band)
    python3 tools/radio_presets.py --file config.json   # compare with a saved answer of the API

The firmware stores the preset's index: new presets are APPENDED to RADIO_PRESETS (and placed in RADIO_PRESET_ORDER),
never inserted. Presets outside the Cap LoRa-1262's band (868-923.5 MHz) are not listed on purpose: Custom covers them.
"""
import json
import os
import re
import sys
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HEADER = os.path.join(ROOT, "app", "core", "RadioPresets.h")
API = "https://api.meshcore.nz/api/v1/config"
CAP_MIN, CAP_MAX = 868.0, 923.5
# Firmware presets that differ from the app on purpose (see RadioPresets.h)
KNOWN = {"EU/UK Narrow": "CR 5, MeshCore's firmware default (the app suggests CR 8)",
         "EU/UK Medium": "no longer in the app; kept for saved settings"}


def firmware():
    text = open(HEADER, encoding="utf-8").read()
    rows = re.findall(r'\{"([^"]+)",\s*([\d.]+)f,\s*([\d.]+)f,\s*(\d+),\s*(\d+),\s*(\d+)\}', text)
    return [(n, float(f), float(b), int(sf), int(cr), int(p)) for n, f, b, sf, cr, p in rows]


def app():
    if "--file" in sys.argv:            # a saved copy of the API's answer
        data = json.load(open(sys.argv[sys.argv.index("--file") + 1], encoding="utf-8"))
    else:
        req = urllib.request.Request(API, headers={"User-Agent": "cardputer-mesh radio_presets.py"})
        with urllib.request.urlopen(req, timeout=20) as r:
            data = json.load(r)
    out = []

    def walk(o):
        if isinstance(o, dict):
            if "frequency" in o:
                ns = o.get("network_settings") or {}
                out.append((o.get("title") or o.get("name"), float(o["frequency"]), float(o["bandwidth"]),
                            int(o["spreading_factor"]), int(o["coding_rate"]), int(ns.get("path_hash_size") or 0)))
            else:
                for v in o.values():
                    walk(v)
        elif isinstance(o, list):
            for v in o:
                walk(v)
    walk(data)
    return out


def same(a, b):
    return abs(a[1] - b[1]) < 0.0005 and abs(a[2] - b[2]) < 0.05 and a[3] == b[3] and a[4] == b[4] and a[5] == b[5]


def main():
    fw, ap = firmware(), app()
    missing = []
    print(f"{'MeshCore app':28} {'MHz':>8} {'BW':>6} SF CR ID   firmware")
    for a in ap:
        match = next((f[0] for f in fw if same(f, a)), None)
        band = CAP_MIN <= a[1] <= CAP_MAX
        note = match or ("-- outside the Cap's band" if not band else "-- MISSING")
        if not match and band:
            missing.append(a)
        print(f"{a[0][:28]:28} {a[1]:8.3f} {a[2]:6g} {a[3]:2} {a[4]:2} {a[5] or '-':>2}   {note}")
    for f in fw:
        if not any(same(f, a) for a in ap):
            print(f"firmware only: {f[0]} ({KNOWN.get(f[0], 'not in the app any more: check')})")
    if "--check" in sys.argv and missing:
        print(f"{len(missing)} app preset(s) missing from RadioPresets.h: append them (never insert)")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
