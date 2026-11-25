"""Erzeugt 1-Bit-Bitmapschriften aus Inter (variable) fuer den Renderer.

Wie Chromium: ungehintet, Graustufen gerastert, dann Schwelle 50 % (entspricht dem
bisherigen n8n-Weg mit Threshold im image-converter). Jede Glyphe liegt in vier
Zwischenpositionen (0, 1/4, 1/2, 3/4 Pixel) vor, damit Buchstaben an ihrer genauen
Position stehen und keine Rundungsluecken entstehen.
Laufweiten und Kerning: HarfBuzz, in 1/16 Pixel.

Aufruf: uv run --with freetype-py --with uharfbuzz python tools/gen_fonts.py
"""
from pathlib import Path

import freetype
import uharfbuzz as hb

import os
FONT_PATH = os.environ.get("INTER_FONT", os.path.expanduser("~/Library/Fonts/Inter-VariableFont_opsz,wght.ttf"))
OUT = Path(__file__).resolve().parent.parent / "lib/render/src/fonts_inter.h"

BASIC = sorted(set([chr(c) for c in range(0x20, 0x7F)] + list("äöüÄÖÜß°–·…")), key=ord)  # sortiert: Binaersuche
FULL = sorted(set(BASIC + [chr(c) for c in range(0xA0, 0x180)] + list("–—‘’‚“”„†•…‰‹›€™←→↑↓✓·")), key=ord)

# Name, Pixelgroesse, Gewicht, Zeichensatz (Masse aus dem n8n-Layout, 1600er Design halbiert)
FONTS = [
    ("inter34b", 34, 800, BASIC),  # Wochentag, Ueberschriften
    ("inter28m", 28, 600, BASIC),  # Datum
    ("inter26b", 26, 800, BASIC),  # Temperatur
    ("inter18m", 18, 600, FULL),   # Aufgabentitel
    ("inter18b", 18, 700, FULL),   # Hinweisbalken
    ("inter17m", 17, 600, FULL),   # Termintitel
    ("inter15b", 15, 700, FULL),   # Uhrzeit, Hinweisbalken
    ("inter14b", 14, 700, FULL),   # Ganztag-Pille, Faelligkeit
    ("inter13b", 13, 700, FULL),   # Wettertext, Fusszeile
]
PHASES = 4
KERN_CHARS = [c for c in BASIC if c.isalnum()] + list(".,:-–()/°")
THRESHOLD = 128


def shape_width(font, upem, size, text):
    buf = hb.Buffer()
    buf.add_str(text)
    buf.guess_segment_properties()
    hb.shape(font, buf, {"kern": True, "liga": False})
    return sum(p.x_advance for p in buf.glyph_positions) * size / upem


def main():
    out = ["// Automatisch erzeugt von tools/gen_fonts.py. Nicht von Hand aendern.",
           "// Schrift: Inter (SIL Open Font License 1.1), rsms.me/inter", "#pragma once", '#include "font.h"', ""]
    for name, size, wght, chars in FONTS:
        opsz = max(14, min(32, size))
        face = freetype.Face(FONT_PATH)
        face.set_var_design_coords((opsz, wght))
        face.set_char_size(size * 64)
        ascent = round(face.size.ascender / 64)
        descent = round(-face.size.descender / 64)
        blob = hb.Blob.from_file_path(FONT_PATH)
        hface = hb.Face(blob)
        hfont = hb.Font(hface)
        hfont.set_variations({"wght": wght, "opsz": opsz})
        upem = hface.upem

        bitmap = bytearray()
        glyphs, bmps = [], []
        for ch in chars:
            if face.get_char_index(ord(ch)) == 0 and ch != " ":
                continue
            adv = shape_width(hfont, upem, size, ch)
            first = len(bmps)
            for ph in range(PHASES):
                delta = freetype.Vector(int(ph * 64 / PHASES), 0)
                face.set_transform(freetype.Matrix(0x10000, 0, 0, 0x10000), delta)
                face.load_char(ch, freetype.FT_LOAD_NO_HINTING | freetype.FT_LOAD_RENDER)
                g = face.glyph
                bm = g.bitmap
                w, h = bm.width, bm.rows
                rows = [bm.buffer[y * bm.pitch:y * bm.pitch + w] for y in range(h)]
                # auf schwarze Pixel zuschneiden
                on = [[v >= THRESHOLD for v in r] for r in rows]
                ys = [y for y in range(h) if any(on[y])]
                xs = [x for x in range(w) if any(on[y][x] for y in range(h))]
                off = len(bitmap)
                if not ys:
                    bmps.append((off, 0, 0, 0, 0))
                    continue
                y0, y1, x0, x1 = ys[0], ys[-1] + 1, xs[0], xs[-1] + 1
                bits = nb = 0
                for y in range(y0, y1):
                    for x in range(x0, x1):
                        bits = (bits << 1) | (1 if on[y][x] else 0)
                        nb += 1
                        if nb == 8:
                            bitmap.append(bits)
                            bits = nb = 0
                if nb:
                    bitmap.append(bits << (8 - nb))
                xo = g.bitmap_left + x0
                yo = -g.bitmap_top + y0
                bmps.append((off, x1 - x0, y1 - y0, xo, yo))
            glyphs.append((ord(ch), round(adv * 16), first))

        kerns = []
        widths = {c: shape_width(hfont, upem, size, c) for c in KERN_CHARS}
        for a in KERN_CHARS:
            for b in KERN_CHARS:
                k16 = round((shape_width(hfont, upem, size, a + b) - widths[a] - widths[b]) * 16)
                if abs(k16) >= 4:
                    kerns.append(((ord(a) << 16) | ord(b), max(-127, min(127, k16))))
        kerns.sort()
        cps = [g[0] for g in glyphs]
        assert cps == sorted(cps), f"{name}: Glyphen nicht sortiert"

        out.append(f"// {name}: {size} px, Gewicht {wght}, {len(glyphs)} Glyphen x {PHASES} Phasen, {len(kerns)} Kerningpaare")
        out.append(f"static const uint8_t {name}_bits[] = {{")
        for i in range(0, len(bitmap), 24):
            out.append("  " + ",".join(f"0x{b:02x}" for b in bitmap[i:i + 24]) + ",")
        out.append("};")
        out.append(f"static const GlyphBmp {name}_bmps[] = {{")
        for off, w, h, xo, yo in bmps:
            out.append(f"  {{{off},{w},{h},{xo},{yo}}},")
        out.append("};")
        out.append(f"static const Glyph {name}_glyphs[] = {{")
        for cp, adv, first in glyphs:
            out.append(f"  {{0x{cp:04x},{adv},{first}}},")
        out.append("};")
        out.append(f"static const KernPair {name}_kern[] = {{")
        for key, k in kerns:
            out.append(f"  {{0x{key:08x},{k}}},")
        if not kerns:
            out.append("  {0,0},")
        out.append("};")
        out.append(f"static const Font {name} = {{{name}_bits, {name}_bmps, {name}_glyphs, {len(glyphs)}, {name}_kern, "
                   f"{len(kerns)}, {size}, {ascent}, {descent}}};")
        out.append("")
        print(name, len(glyphs), "Glyphen", len(bitmap), "Byte", len(kerns), "Kerning")
    OUT.write_text("\n".join(out) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
