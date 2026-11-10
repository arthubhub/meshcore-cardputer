#!/usr/bin/env python3
"""Cover image for the M5Burner listing (RELEASE-3): real English screenshots of the simulator on a dark card.

    make -C app/sim run            # the screenshots (build/shots/) - the README ones are in docs/images/
    python3 tools/m5burner_cover.py [-o docs/images/m5burner_cover.png]

1920x1080 (16:9). Screenshots are scaled to exactly 2x the 240x135 screen with nearest-neighbour, so the pixel font stays
sharp. The text says what the firmware is - built on MeshCore - without MeshCore's logo: this is not an official
MeshCore image.
"""
import argparse
import os
import sys

from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SHOTS = os.path.join(ROOT, "app", "sim", "build", "shots")
IMAGES = os.path.join(ROOT, "docs", "images")
FONTS = "/usr/share/fonts/truetype/noto"

W, H = 1920, 1080
SCREEN = (240, 135)        # the Cardputer's display; shots are saved 3x
SCALE = 2
BG_TOP, BG_BOTTOM = (10, 14, 24), (16, 26, 40)
ACCENT = (0, 190, 255)     # the cyan of the firmware's own UI (theme::ACCENT)
TEXT, DIM = (236, 242, 248), (140, 156, 176)
FRAME = (38, 50, 68)

# (file, caption): what a Cardputer owner should see first
PANELS = [
    (os.path.join(IMAGES, "readme_en_home.png"), "Home: messages and neighbours"),
    (os.path.join(IMAGES, "readme_en_chat.png"), "Chat on the keyboard, delivery receipts"),
    (os.path.join(IMAGES, "readme_en_map.png"), "Offline maps from the SD card"),
    (os.path.join(SHOTS, "c2_compass_walking.png"), "GPS compass towards a contact"),
    (os.path.join(SHOTS, "n2_nearby_found.png"), "Repeaters in radio range"),
    (os.path.join(SHOTS, "p2_list.png"), "Packet log and radio tools"),
]


def font(name, size):
    path = os.path.join(FONTS, name)
    if not os.path.exists(path):
        sys.exit(f"missing font {path}")
    return ImageFont.truetype(path, size)


def screenshot(path):
    if not os.path.exists(path):
        sys.exit(f"missing screenshot {path}: run `make -C app/sim run` first")
    img = Image.open(path).convert("RGB")
    native = img.resize(SCREEN, Image.NEAREST)          # back to one pixel per screen pixel
    return native.resize((SCREEN[0] * SCALE, SCREEN[1] * SCALE), Image.NEAREST)


def gradient():
    img = Image.new("RGB", (W, H))
    d = ImageDraw.Draw(img)
    for y in range(H):
        t = y / (H - 1)
        d.line([(0, y), (W, y)], fill=tuple(int(a + (b - a) * t) for a, b in zip(BG_TOP, BG_BOTTOM)))
    return img


def mesh_pattern(d):
    """Faint nodes and links in the top right corner: a mesh, not a logo."""
    nodes = [(1500, 90), (1640, 60), (1760, 130), (1690, 210), (1560, 200), (1840, 60), (1860, 220)]
    links = [(0, 1), (1, 2), (2, 3), (3, 4), (4, 0), (1, 5), (2, 6), (3, 6), (0, 3)]
    for a, b in links:
        d.line([nodes[a], nodes[b]], fill=(30, 70, 100), width=3)
    for x, y in nodes:
        d.ellipse([x - 9, y - 9, x + 9, y + 9], fill=(20, 110, 160), outline=ACCENT, width=2)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-o", "--output", default=os.path.join(IMAGES, "m5burner_cover.png"))
    args = ap.parse_args()

    img = gradient()
    d = ImageDraw.Draw(img)
    mesh_pattern(d)

    title, sub, small = font("NotoSans-Bold.ttf", 78), font("NotoSans-Regular.ttf", 36), font("NotoSans-Regular.ttf", 27)
    d.text((90, 64), "Cardputer Mesh", font=title, fill=TEXT)
    d.text((90, 166), "Standalone LoRa mesh messenger for the M5Stack Cardputer ADV + Cap LoRa-1262",
           font=sub, fill=ACCENT)
    d.text((90, 222), "Built on MeshCore  ·  no phone needed  ·  phone mode for the MeshCore app (dual boot)",
           font=small, fill=DIM)

    pw, ph = SCREEN[0] * SCALE, SCREEN[1] * SCALE    # 480 x 270
    cols, gap_x, gap_y, caption_h = 3, 66, 34, 50
    grid_w = cols * pw + (cols - 1) * gap_x
    x0, y0 = (W - grid_w) // 2, 300
    cap = font("NotoSans-Regular.ttf", 26)
    for i, (path, caption) in enumerate(PANELS):
        col, row = i % cols, i // cols
        x, y = x0 + col * (pw + gap_x), y0 + row * (ph + caption_h + gap_y)
        d.rounded_rectangle([x - 10, y - 10, x + pw + 10, y + ph + 10], radius=16, fill=FRAME)
        img.paste(screenshot(path), (x, y))
        tw = d.textlength(caption, font=cap)
        d.text((x + (pw - tw) / 2, y + ph + 18), caption, font=cap, fill=TEXT)

    foot = font("NotoSans-Regular.ttf", 24)
    note = "Screenshots from the firmware's own interface (PC simulator). English and French UI. Open source."
    d.text(((W - d.textlength(note, font=foot)) / 2, H - 50), note, font=foot, fill=DIM)

    os.makedirs(os.path.dirname(args.output), exist_ok=True)
    img.save(args.output, optimize=True)
    print(f"written {args.output} ({W}x{H}, {os.path.getsize(args.output) // 1024} KB)")


if __name__ == "__main__":
    main()
