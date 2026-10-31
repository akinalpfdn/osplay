"""Generates the README download buttons in artwork/readme/ with the text as outlines, so every
viewer sees the same width whatever fonts their system has.

Usage, with Inter SemiBold from https://github.com/rsms/inter/releases (SIL Open Font License):
    uv run --no-project --with fonttools --with uharfbuzz python scripts/readme/make_download_buttons.py \
        <path to Inter-SemiBold.ttf> artwork/readme
"""
import sys
from pathlib import Path

import uharfbuzz as hb
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.ttLib import TTFont

FONT_FILE, OUT = Path(sys.argv[1]), Path(sys.argv[2])
APPLE = ('M12.152 6.896c-.948 0-2.415-1.078-3.96-1.04-2.04.027-3.91 1.183-4.961 3.014-2.117 3.675-.546 9.103 1.519 12.09 '
         '1.013 1.454 2.208 3.09 3.792 3.039 1.52-.065 2.09-.987 3.935-.987 1.831 0 2.35.987 3.96.948 1.637-.026 2.676-1.48 '
         '3.676-2.948 1.156-1.688 1.636-3.325 1.662-3.415-.039-.013-3.182-1.221-3.22-4.857-.026-3.04 2.48-4.494 2.597-4.559'
         '-1.429-2.09-3.623-2.324-4.39-2.376-2-.156-3.675 1.09-4.61 1.09zM15.53 3.83c.843-1.012 1.4-2.427 1.245-3.83-1.207.052'
         '-2.662.805-3.532 1.818-.78.896-1.454 2.338-1.273 3.714 1.338.104 2.715-.688 3.559-1.701')
ANDROID = ('M18.4395 5.5586c-.675 1.1664-1.352 2.3318-2.0274 3.498-.0366-.0155-.0742-.0286-.1113-.043-1.8249-.6957-3.484'
           '-.8-4.42-.787-1.8551.0185-3.3544.4643-4.2597.8203-.084-.1494-1.7526-3.021-2.0215-3.4864a1.1451 1.1451 0 0 0'
           '-.1406-.1914c-.3312-.364-.9054-.4859-1.379-.203-.475.282-.7136.9361-.3886 1.5019 1.9466 3.3696-.0966-.2158 '
           '1.9473 3.3593.0172.031-.4946.2642-1.3926 1.0177C2.8987 12.176.452 14.772 0 18.9902h24c-.119-1.1108-.3686'
           '-2.099-.7461-3.0683-.7438-1.9118-1.8435-3.2928-2.7402-4.1836a12.1048 12.1048 0 0 0-2.1309-1.6875c.6594-1.122 '
           '1.312-2.2559 1.9649-3.3848.2077-.3615.1886-.7956-.0079-1.1191a1.1001 1.1001 0 0 0-.8515-.5332c-.5225-.0536'
           '-.9392.3128-1.0488.5449zm-.0391 8.461c.3944.5926.324 1.3306-.1563 1.6503-.4799.3197-1.188.0985-1.582-.4941'
           '-.3944-.5927-.324-1.3307.1563-1.6504.4727-.315 1.1812-.1086 1.582.4941zM7.207 13.5273c.4803.3197.5506 '
           '1.0577.1563 1.6504-.394.5926-1.1038.8138-1.584.4941-.48-.3197-.5503-1.0577-.1563-1.6504.4008-.6021 1.1087'
           '-.8106 1.584-.4941z')

font = TTFont(FONT_FILE)
glyphs = font.getGlyphSet()
upem = font["head"].unitsPerEm
cap = font["OS/2"].sCapHeight
blob = hb.Blob(FONT_FILE.read_bytes())
hb_font = hb.Font(hb.Face(blob))


def text_path(text, size, x, baseline):
    buf = hb.Buffer()
    buf.add_str(text)
    buf.guess_segment_properties()
    hb.shape(hb_font, buf, {"kern": True, "liga": True})
    scale = size / upem
    pen = SVGPathPen(glyphs)
    cursor = 0
    order = font.getGlyphOrder()
    for info, pos in zip(buf.glyph_infos, buf.glyph_positions):
        name = order[info.codepoint]
        transform = (scale, 0, 0, -scale, x + (cursor + pos.x_offset) * scale, baseline - pos.y_offset * scale)
        glyphs[name].draw(TransformPen(pen, transform))
        cursor += pos.x_advance
    return pen.getCommands(), cursor * scale


HEIGHT, PAD, GAP, SIZE, ICON = 48, 18, 10, 17, 20


def button(name, label, text, background, border, icon_svg, icon_width):
    baseline = HEIGHT / 2 + cap * SIZE / upem / 2
    text_x = PAD + icon_width + GAP
    path, width = text_path(text, SIZE, text_x, baseline)
    total = round(text_x + width + PAD)
    svg = f'''<svg xmlns="http://www.w3.org/2000/svg" width="{total}" height="{HEIGHT}" viewBox="0 0 {total} {HEIGHT}" role="img" aria-label="{label}">
  <title>{label}</title>
  <rect x="0.5" y="0.5" width="{total - 1}" height="{HEIGHT - 1}" rx="12" fill="{background}" stroke="{border}"/>
  {icon_svg}
  <path fill="#ffffff" d="{path}"/>
</svg>
'''
    (OUT / name).write_text(svg)
    print(name, total)


apple_scale = ICON / 24
apple = f'<path transform="translate({PAD - 2.3 * apple_scale:.2f} {(HEIGHT - ICON) / 2 - 0.5:.2f}) scale({apple_scale:.4f})" fill="#ffffff" d="{APPLE}"/>'
square, gap = 8.3, 1.4
win_size = square * 2 + gap
win_y = (HEIGHT - win_size) / 2
windows = (f'<g fill="#ffffff" transform="translate({PAD} {win_y:.2f})">'
           f'<rect width="{square}" height="{square}"/><rect x="{square + gap}" width="{square}" height="{square}"/>'
           f'<rect y="{square + gap}" width="{square}" height="{square}"/>'
           f'<rect x="{square + gap}" y="{square + gap}" width="{square}" height="{square}"/></g>')

# The robot's head spans y 5.2-19 of its 24-unit box; it is centred on that, a little wider than
# the other icons so it reads at the same weight.
android_scale = 22 / 24
android = (f'<path transform="translate({PAD:.2f} {HEIGHT / 2 - 12.1 * android_scale:.2f}) scale({android_scale:.4f})" '
           f'fill="#ffffff" d="{ANDROID}"/>')

MAC = ("#18181b", "#3f3f46", apple, 19.2 * apple_scale)
WIN = ("#0067c0", "#0b78d0", windows, win_size)
# Android's green (#3ddc84) is too light for white text; a darker shade of it keeps the contrast.
DROID = ("#0b7a43", "#12935a", android, 24 * android_scale)
button("download-macos.svg", "Download Osplay for macOS", "Download for macOS", *MAC)
button("download-windows.svg", "Download Osplay for Windows", "Download for Windows", *WIN)
button("download-android.svg", "Download Osplay for Android", "Download for Android", *DROID)
button("download-macos.tr.svg", "Osplay'i macOS için indir", "macOS için indir", *MAC)
button("download-windows.tr.svg", "Osplay'i Windows için indir", "Windows için indir", *WIN)
button("download-android.tr.svg", "Osplay'i Android için indir", "Android için indir", *DROID)
