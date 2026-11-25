#pragma once
#include "canvas.h"
#include "font.h"

uint32_t utf8Next(const char *&p, const char *end);
const Glyph *findGlyph(const Font &f, uint32_t cp);
int kern16(const Font &f, uint32_t a, uint32_t b);

// Breite in 1/16 Pixel bzw. gerundet in Pixel. len < 0 = bis zum Nullbyte.
int textWidth16(const Font &f, const char *s, int len = -1);
inline int textWidth(const Font &f, const char *s, int len = -1) { return (textWidth16(f, s, len) + 8) / 16; }

// Zeichnet ab x (linke Kante) auf der Grundlinie, liefert die Breite in Pixel.
int drawText(Canvas &c, const Font &f, int x, int baseline, const char *s, int len = -1, Color col = BLACK);
int drawTextRight(Canvas &c, const Font &f, int xRight, int baseline, const char *s, Color col = BLACK);

struct TextLine {
  int start;      // Byte-Offset im Quelltext
  int len;        // Laenge in Byte
  bool hyphen;    // Trennstrich anhaengen
  bool ellipsis;  // "…" anhaengen (Text gekuerzt)
};

// Bricht Text auf Zeilen von hoechstens maxW Pixel um. Woerter, die allein
// nicht in eine Zeile passen, werden getrennt. Liefert die Zeilenzahl.
int wrapText(const Font &f, const char *s, int maxW, TextLine *lines, int maxLines);
int lineWidth(const Font &f, const char *s, const TextLine &l);
int drawLine(Canvas &c, const Font &f, int x, int baseline, const char *s, const TextLine &l, Color col = BLACK);
