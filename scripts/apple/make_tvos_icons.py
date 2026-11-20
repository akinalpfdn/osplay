"""Generates the Apple TV app's brand assets in macos/OsplayTV/Resources/Assets.xcassets: the layered
app icon (a blue back layer and the white monitor artwork in front, so tvOS can tilt them apart),
its App Store version, and the top shelf images with the app name.

Usage, with Inter SemiBold from https://github.com/rsms/inter/releases (SIL Open Font License):
    uv run --no-project --with pillow python scripts/apple/make_tvos_icons.py <path to Inter-SemiBold.ttf>
"""
import json
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent.parent
FONT_FILE = Path(sys.argv[1])
CATALOG = ROOT / "macos/OsplayTV/Resources/Assets.xcassets"
BRAND_ASSETS = CATALOG / "App Icon & Top Shelf Image.brandassets"
ARTWORK = ROOT / "artwork/icons/osplay-white-1024.png"
TOP = (0x2A, 0x5F, 0xF0)  # a lighter Osplay blue at the top, for depth
BOTTOM = (0x1D, 0x4E, 0xD8)  # Osplay's blue (OsplayColors.Brand)
ARTWORK_SHARE = 0.62  # of the icon's height
INFO = {"author": "xcode", "version": 1}


def write_json(folder: Path, content: dict) -> None:
    folder.mkdir(parents=True, exist_ok=True)
    (folder / "Contents.json").write_text(json.dumps(content, indent=2) + "\n")


def gradient(width: int, height: int) -> Image.Image:
    image = Image.new("RGB", (width, height))
    draw = ImageDraw.Draw(image)
    for y in range(height):
        t = y / max(height - 1, 1)
        draw.line([(0, y), (width, y)], fill=tuple(round(a + (b - a) * t) for a, b in zip(TOP, BOTTOM)))
    return image


def artwork(width: int, height: int) -> Image.Image:
    layer = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    side = round(height * ARTWORK_SHARE)
    monitor = Image.open(ARTWORK).convert("RGBA").resize((side, side), Image.LANCZOS)
    layer.paste(monitor, ((width - side) // 2, (height - side) // 2), monitor)
    return layer


def top_shelf(width: int, height: int) -> Image.Image:
    image = gradient(width, height).convert("RGBA")
    side = round(height * 0.42)
    font = ImageFont.truetype(str(FONT_FILE), round(height * 0.22))
    draw = ImageDraw.Draw(image)
    left, top, right, bottom = draw.textbbox((0, 0), "Osplay", font=font)
    gap = round(height * 0.06)
    total = side + gap + (right - left)
    x = (width - total) // 2
    monitor = Image.open(ARTWORK).convert("RGBA").resize((side, side), Image.LANCZOS)
    image.paste(monitor, (x, (height - side) // 2), monitor)
    draw.text((x + side + gap - left, (height - (bottom - top)) // 2 - top), "Osplay", font=font, fill="white")
    return image.convert("RGB")


def image_stack(name: str, width: int, height: int, scales: list[int], idiom: str) -> None:
    stack = BRAND_ASSETS / f"{name}.imagestack"
    write_json(stack, {"info": INFO, "layers": [{"filename": "Front.imagestacklayer"}, {"filename": "Back.imagestacklayer"}]})
    for layer, draw in (("Front", artwork), ("Back", gradient)):
        folder = stack / f"{layer}.imagestacklayer"
        write_json(folder, {"info": INFO})
        content = folder / "Content.imageset"
        content.mkdir(parents=True, exist_ok=True)
        images = []
        for scale in scales:
            filename = f"{layer.lower()}@{scale}x.png"
            draw(width * scale, height * scale).save(content / filename)
            images.append({"filename": filename, "idiom": idiom, "scale": f"{scale}x"})
        write_json(content, {"images": images, "info": INFO})


def image_set(name: str, width: int, height: int) -> None:
    folder = BRAND_ASSETS / f"{name}.imageset"
    folder.mkdir(parents=True, exist_ok=True)
    images = []
    for scale in (1, 2):
        filename = f"top-shelf@{scale}x.png"
        top_shelf(width * scale, height * scale).save(folder / filename)
        images.append({"filename": filename, "idiom": "tv", "scale": f"{scale}x"})
    write_json(folder, {"images": images, "info": INFO})


write_json(CATALOG, {"info": INFO})
image_stack("App Icon", 400, 240, [1, 2], "tv")
image_stack("App Icon - App Store", 1280, 768, [1], "tv")
image_set("Top Shelf Image", 1920, 720)
image_set("Top Shelf Image Wide", 2320, 720)
write_json(BRAND_ASSETS, {
    "assets": [
        {"filename": "App Icon - App Store.imagestack", "idiom": "tv", "role": "primary-app-icon", "size": "1280x768"},
        {"filename": "App Icon.imagestack", "idiom": "tv", "role": "primary-app-icon", "size": "400x240"},
        {"filename": "Top Shelf Image Wide.imageset", "idiom": "tv", "role": "top-shelf-image-wide", "size": "2320x720"},
        {"filename": "Top Shelf Image.imageset", "idiom": "tv", "role": "top-shelf-image", "size": "1920x720"},
    ],
    "info": INFO,
})
print("wrote", BRAND_ASSETS.relative_to(ROOT))
