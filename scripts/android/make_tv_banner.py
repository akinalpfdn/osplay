"""Generates the Android TV launcher banner (320 x 180 px, xhdpi): the white monitor artwork and
the app name on Osplay's blue.

Usage, with Inter SemiBold from https://github.com/rsms/inter/releases (SIL Open Font License):
    uv run --no-project --with pillow python scripts/android/make_tv_banner.py \
        <path to Inter-SemiBold.ttf> android/app/src/main/res/drawable-xhdpi/tv_banner.png
"""
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent.parent
FONT_FILE, OUT = Path(sys.argv[1]), Path(sys.argv[2])
WIDTH, HEIGHT = 320, 180
BRAND = (0x1D, 0x4E, 0xD8)  # OsplayColors.Brand
ICON_SIZE, GAP, TEXT_SIZE = 72, 14, 40
SCALE = 4  # drawn larger, then reduced, for smooth edges

canvas = Image.new("RGB", (WIDTH * SCALE, HEIGHT * SCALE), BRAND)
icon = Image.open(ROOT / "artwork/icons/osplay-white-512.png").convert("RGBA")
icon = icon.resize((ICON_SIZE * SCALE, ICON_SIZE * SCALE), Image.LANCZOS)
font = ImageFont.truetype(str(FONT_FILE), TEXT_SIZE * SCALE)
draw = ImageDraw.Draw(canvas)
left, top, right, bottom = draw.textbbox((0, 0), "Osplay", font=font)
text_width = right - left
total = ICON_SIZE * SCALE + GAP * SCALE + text_width
x = (WIDTH * SCALE - total) // 2
canvas.paste(icon, (x, (HEIGHT * SCALE - ICON_SIZE * SCALE) // 2), icon)
text_x = x + ICON_SIZE * SCALE + GAP * SCALE - left
text_y = (HEIGHT * SCALE - (bottom - top)) // 2 - top
draw.text((text_x, text_y), "Osplay", font=font, fill=(255, 255, 255))
OUT.parent.mkdir(parents=True, exist_ok=True)
canvas.resize((WIDTH, HEIGHT), Image.LANCZOS).save(OUT, optimize=True)
print(OUT, WIDTH, HEIGHT)
