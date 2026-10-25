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

MAC = ("#18181b", "#3f3f46", apple, 19.2 * apple_scale)
WIN = ("#0067c0", "#0b78d0", windows, win_size)
button("download-macos.svg", "Download Osplay for macOS", "Download for macOS", *MAC)
button("download-windows.svg", "Download Osplay for Windows", "Download for Windows", *WIN)
button("download-macos.tr.svg", "Osplay'i macOS için indir", "macOS için indir", *MAC)
button("download-windows.tr.svg", "Osplay'i Windows için indir", "Windows için indir", *WIN)
