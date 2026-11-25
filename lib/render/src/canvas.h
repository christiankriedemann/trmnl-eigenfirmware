#pragma once
#include <stdint.h>
#include <string.h>

// 1-Bit-Zeichenflaeche 800 x 480 im Format von bb_epaper:
// Bit 1 = weiss, Bit 0 = schwarz, linkes Pixel im hoechsten Bit, 100 Byte je Zeile.

static const int CANVAS_W = 800;
static const int CANVAS_H = 480;
static const int CANVAS_BYTES = CANVAS_W * CANVAS_H / 8;

enum Color : uint8_t { BLACK = 0, WHITE = 1 };

class Canvas {
 public:
  explicit Canvas(uint8_t *buffer) : buf(buffer) {}
  uint8_t *buf;

  void clear(Color c) { memset(buf, c == WHITE ? 0xFF : 0x00, CANVAS_BYTES); }
  void setClip(int x0, int y0, int x1, int y1) { cx0 = x0; cy0 = y0; cx1 = x1; cy1 = y1; }
  void resetClip() { cx0 = 0; cy0 = 0; cx1 = CANVAS_W; cy1 = CANVAS_H; }

  inline void px(int x, int y, Color c) {
    if (x < cx0 || x >= cx1 || y < cy0 || y >= cy1) return;
    uint8_t *p = &buf[y * (CANVAS_W / 8) + (x >> 3)];
    uint8_t m = 0x80 >> (x & 7);
    if (c == WHITE) *p |= m; else *p &= ~m;
  }
  void fillRect(int x, int y, int w, int h, Color c);
  void fillRoundRect(int x, int y, int w, int h, int r, Color c);
  void rect(int x, int y, int w, int h, int t, Color c);
  // Bitmap mit auf Bytes aufgefuellten Zeilen, gesetzte Bits werden in Farbe c gezeichnet
  void blitRows(const uint8_t *bits, int w, int h, int x, int y, Color c);
  // Bitmap ohne Auffuellung (Glyphen)
  void blitPacked(const uint8_t *bits, int w, int h, int x, int y, Color c);

 private:
  int cx0 = 0, cy0 = 0, cx1 = CANVAS_W, cy1 = CANVAS_H;
};
