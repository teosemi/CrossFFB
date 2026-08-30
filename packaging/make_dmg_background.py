#!/usr/bin/env python3
"""Regenerates the disk image background.

The PNG beside this script is what the release actually uses; this is kept so
the image can be changed rather than only replaced. Needs Pillow:

    pip install Pillow

Run it from this folder; it overwrites dmg-background.png.
"""

from PIL import Image, ImageDraw, ImageFont
from pathlib import Path

W, H = 600, 480
out = Path("dmg-background.png")

img = Image.new("RGB", (W, H), (246, 246, 246))
draw = ImageDraw.Draw(img)

def font(size):
    candidates = [
        "/System/Library/Fonts/SFNS.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
    ]
    for p in candidates:
        if Path(p).exists():
            return ImageFont.truetype(p, size)
    return ImageFont.load_default()

arrow_font = font(58)

# Freccia centrale, resta nella stessa posizione
draw.text((W / 2, 130), ">", fill=(120, 120, 120), font=arrow_font, anchor="mm")

img.save(out)
print(f"Created {out.resolve()}")
