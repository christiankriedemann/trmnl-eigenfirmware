#include "canvas.h"

void Canvas::fillRect(int x, int y, int w, int h, Color c) {
  for (int yy = y; yy < y + h; yy++)
    for (int xx = x; xx < x + w; xx++) px(xx, yy, c);
}

void Canvas::fillRoundRect(int x, int y, int w, int h, int r, Color c) {
  for (int yy = 0; yy < h; yy++) {
    for (int xx = 0; xx < w; xx++) {
      int dx = 0, dy = 0;
      if (xx < r) dx = r - xx - 1; else if (xx >= w - r) dx = xx - (w - r);
      if (yy < r) dy = r - yy - 1; else if (yy >= h - r) dy = yy - (h - r);
      if (dx && dy && dx * dx + dy * dy > r * r) continue;
      px(x + xx, y + yy, c);
    }
  }
}

void Canvas::rect(int x, int y, int w, int h, int t, Color c) {
  fillRect(x, y, w, t, c);
  fillRect(x, y + h - t, w, t, c);
  fillRect(x, y, t, h, c);
  fillRect(x + w - t, y, t, h, c);
}

void Canvas::blitRows(const uint8_t *bits, int w, int h, int x, int y, Color c) {
  int stride = (w + 7) / 8;
  for (int yy = 0; yy < h; yy++)
    for (int xx = 0; xx < w; xx++)
      if (bits[yy * stride + (xx >> 3)] & (0x80 >> (xx & 7))) px(x + xx, y + yy, c);
}

void Canvas::blitPacked(const uint8_t *bits, int w, int h, int x, int y, Color c) {
  int n = 0;
  for (int yy = 0; yy < h; yy++)
    for (int xx = 0; xx < w; xx++, n++)
      if (bits[n >> 3] & (0x80 >> (n & 7))) px(x + xx, y + yy, c);
}
