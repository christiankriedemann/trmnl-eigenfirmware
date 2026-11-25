#pragma once
#include <stdint.h>

// Bitmap einer Glyphe in einer Zwischenposition, Zeilen ohne Auffuellung (MSB zuerst).
struct GlyphBmp {
  uint32_t offset;  // Startbyte in bits[]
  uint8_t w, h;
  int8_t xo, yo;    // Versatz zum Ursprung (yo relativ zur Grundlinie, negativ = oben)
};

static const int GLYPH_PHASES = 4;  // Zwischenpositionen je Pixel: 0, 1/4, 1/2, 3/4

struct Glyph {
  uint16_t cp;      // Unicode-Codepunkt
  uint16_t adv16;   // Vorschub in 1/16 Pixel
  uint16_t first;   // Index der ersten von GLYPH_PHASES Bitmaps
};

struct KernPair {
  uint32_t key;  // (links << 16) | rechts
  int8_t k16;    // Korrektur in 1/16 Pixel
};

struct Font {
  const uint8_t *bits;
  const GlyphBmp *bmps;
  const Glyph *glyphs;
  uint16_t count;
  const KernPair *kern;
  uint16_t kernCount;
  uint8_t size;
  uint8_t ascent;
  uint8_t descent;
};
