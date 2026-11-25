"""Erzeugt lib/render/src/hyph_de.h aus den TeX-Trennmustern (hyph-de-1996, neue Rechtschreibung).

Speicherformat (Front-Coding in Bloecken zu 32 Mustern):
  je Muster: 1 Byte (gemeinsamer Praefix << 4 | Suffixlaenge), Suffix-Codes,
             1 Byte Anzahl Werte, je Wert 1 Byte (Position << 4 | Wert)
  Blockanfaenge haben Praefix 0. hyph_blocks[] enthaelt deren Offsets.
Lizenz der Muster: siehe Kopf der Musterdatei (MIT-artig, tex-hyphen).
"""
import re
from pathlib import Path

base = Path(__file__).resolve().parent
pats = (base / "hyph/hyph-de-1996.pat.txt").read_text(encoding="utf-8").split()
alphabet = sorted({c for p in pats for c in re.sub(r"\d", "", p)})
code = {c: i + 1 for i, c in enumerate(alphabet)}

entries = []
for p in pats:
    letters, vals = [], []
    pos = 0
    for ch in p:
        if ch.isdigit():
            vals.append((pos, int(ch)))
        else:
            letters.append(code[ch])
            pos += 1
    entries.append((bytes(letters), vals))
entries.sort(key=lambda e: e[0])

BLOCK = 32
data = bytearray()
blocks = []
prev = b""
for i, (key, vals) in enumerate(entries):
    if i % BLOCK == 0:
        blocks.append(len(data))
        prev = b""
    pre = 0
    while pre < min(len(prev), len(key), 15) and prev[pre] == key[pre]:
        pre += 1
    suf = key[pre:]
    assert len(suf) < 16
    data.append((pre << 4) | len(suf))
    data += suf
    data.append(len(vals))
    for pos, v in vals:
        data.append((pos << 4) | v)
    prev = key

out = ["// Automatisch erzeugt von tools/gen_hyph.py aus hyph-de-1996 (tex-hyphen). Nicht von Hand aendern.",
       "#pragma once", "#include <stdint.h>", ""]
cps = ",".join(str(ord(c)) for c in alphabet)
out.append(f"static const uint16_t hyph_alphabet[] = {{{cps}}};  // Code = Index + 1")
out.append(f"static const int hyph_alphabet_len = {len(alphabet)};")
out.append(f"static const int hyph_count = {len(entries)};")
out.append(f"static const int hyph_block = {BLOCK};")
out.append(f"static const int hyph_nblocks = {len(blocks)};")
out.append("static const uint32_t hyph_blocks[] = {" + ",".join(map(str, blocks)) + "};")
out.append("static const uint8_t hyph_data[] = {")
for i in range(0, len(data), 32):
    out.append("  " + ",".join(str(b) for b in data[i:i + 32]) + ",")
out.append("};")
(base.parent / "lib/render/src/hyph_de.h").write_text("\n".join(out) + "\n")
print(len(entries), "Muster,", len(data), "Byte Daten,", len(blocks), "Bloecke")
