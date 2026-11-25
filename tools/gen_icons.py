"""Wandelt tools/icons/*-22.png und *-44.png in lib/render/src/icons.h (1 Bit, Zeilen auf Bytes aufgefuellt, 1 = schwarz)."""
from pathlib import Path
from PIL import Image
base = Path(__file__).resolve().parent
out = ["// Automatisch erzeugt von tools/gen_icons.py", "#pragma once", "#include <stdint.h>", "",
       "struct Icon { uint8_t w, h; const uint8_t *bits; };", ""]
names = ["sun", "cloud", "rain", "snow", "storm"]
for size in (22, 44):
    for n in names:
        im = Image.open(base / "icons" / f"{n}-{size}.png").convert("1")
        w, h = im.size
        px = im.load()
        data = []
        for y in range(h):
            for xb in range(0, w, 8):
                b = 0
                for bit in range(8):
                    x = xb + bit
                    b = (b << 1) | (1 if x < w and px[x, y] == 0 else 0)
                data.append(b)
        out.append(f"static const uint8_t icon_{n}{size}_bits[] = {{{','.join(f'0x{v:02x}' for v in data)}}};")
        out.append(f"static const Icon icon_{n}{size} = {{{w},{h},icon_{n}{size}_bits}};")
(base.parent / "lib/render/src/icons.h").write_text("\n".join(out) + "\n")
print("ok")
