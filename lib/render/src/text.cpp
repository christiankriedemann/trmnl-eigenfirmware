#include "text.h"

#include <string.h>

#include "hyph.h"

uint32_t utf8Next(const char *&p, const char *end) {
  if (p >= end) return 0;
  uint8_t c = (uint8_t)*p++;
  if (c < 0x80) return c;
  int n = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : (c >= 0xC0) ? 1 : 0;
  uint32_t cp = c & (0x3F >> n);
  for (int i = 0; i < n && p < end; i++) cp = (cp << 6) | ((uint8_t)*p++ & 0x3F);
  return cp;
}

const Glyph *findGlyph(const Font &f, uint32_t cp) {
  int lo = 0, hi = f.count - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    if (f.glyphs[mid].cp == cp) return &f.glyphs[mid];
    if (f.glyphs[mid].cp < cp) lo = mid + 1; else hi = mid - 1;
  }
  return nullptr;
}

static const Glyph *glyphOrFallback(const Font &f, uint32_t cp) {
  const Glyph *g = findGlyph(f, cp);
  if (g) return g;
  if (cp == 0x2026) return findGlyph(f, '.');
  if (cp >= 0x1F000 || (cp >= 0x2600 && cp <= 0x27BF) || cp == 0xFE0F || cp == 0x200D) return nullptr;  // Emoji weglassen
  return findGlyph(f, '?');
}

int kern16(const Font &f, uint32_t a, uint32_t b) {
  if (a > 0xFFFF || b > 0xFFFF) return 0;
  uint32_t key = (a << 16) | b;
  int lo = 0, hi = f.kernCount - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    if (f.kern[mid].key == key) return f.kern[mid].k16;
    if (f.kern[mid].key < key) lo = mid + 1; else hi = mid - 1;
  }
  return 0;
}

int textWidth16(const Font &f, const char *s, int len) {
  if (len < 0) len = strlen(s);
  const char *p = s, *end = s + len;
  int w = 0;
  uint32_t prev = 0;
  while (p < end) {
    uint32_t cp = utf8Next(p, end);
    const Glyph *g = glyphOrFallback(f, cp);
    if (!g) continue;
    if (prev) w += kern16(f, prev, cp);
    w += g->adv16;
    prev = cp;
  }
  return w;
}

int drawText(Canvas &c, const Font &f, int x, int baseline, const char *s, int len, Color col) {
  if (len < 0) len = strlen(s);
  const char *p = s, *end = s + len;
  int pos16 = 0;
  uint32_t prev = 0;
  while (p < end) {
    uint32_t cp = utf8Next(p, end);
    const Glyph *g = glyphOrFallback(f, cp);
    if (!g) continue;
    if (prev) pos16 += kern16(f, prev, cp);
    // Zwischenposition waehlen: ganze Pixel plus Viertelpixel
    int q = (pos16 + 2) >> 2;  // in Viertelpixeln, gerundet
    int px = q >> 2, phase = q & 3;
    const GlyphBmp &b = f.bmps[g->first + phase];
    if (b.w && b.h) c.blitPacked(f.bits + b.offset, b.w, b.h, x + px + b.xo, baseline + b.yo, col);
    pos16 += g->adv16;
    prev = cp;
  }
  return (pos16 + 8) / 16;
}

int drawTextRight(Canvas &c, const Font &f, int xRight, int baseline, const char *s, Color col) {
  int w = textWidth(f, s);
  drawText(c, f, xRight - w, baseline, s, -1, col);
  return w;
}

// ---------- Umbruch ----------

static bool isLetter(uint32_t cp) {
  return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || (cp >= 0xC0 && cp <= 0x24F && cp != 0xD7 && cp != 0xF7);
}

// Sucht im Token [ts, te) die am weitesten rechts liegende Trennstelle, bei der
// Zeile [lineStart, Stelle) + "-" in maxW16 passt. Trennstellen kommen aus den
// deutschen Trennmustern. Liefert den Byte-Offset der Stelle im Text oder -1.
static int hyphenBreak(const Font &f, const char *s, int lineStart, int ts, int te, int maxW16) {
  // Adressen und Dateinamen (enthalten '.', '/', '@' oder ':' mitten im Wort) nie trennen
  for (int i = ts; i < te - 1; i++)
    if ((s[i] == '.' || s[i] == '/' || s[i] == '@' || s[i] == ':') && s[i + 1] != ' ') return -1;
  const int MAXC = 60;
  int off[MAXC + 1];
  uint32_t cps[MAXC];
  int n = 0;
  const char *p = s + ts, *end = s + te;
  while (p < end && n < MAXC) {
    off[n] = p - s;
    cps[n] = utf8Next(p, end);
    n++;
  }
  off[n] = p - s;
  // laengste Buchstabenfolge im Token (ohne Klammern, Kommas usw.)
  int bestA = 0, bestB = 0;
  for (int i = 0; i < n;) {
    if (!isLetter(cps[i])) { i++; continue; }
    int j = i;
    while (j < n && isLetter(cps[j])) j++;
    if (j - i > bestB - bestA) { bestA = i; bestB = j; }
    i = j;
  }
  int len = bestB - bestA;
  if (len < 5) return -1;
  uint8_t br[MAXC + 1];
  if (!hyphenate(cps + bestA, len, br)) return -1;
  int hyph16 = textWidth16(f, "-");
  for (int k = len - 1; k >= 1; k--) {
    if (!br[k]) continue;
    int at = off[bestA + k];
    if (textWidth16(f, s + lineStart, at - lineStart) + hyph16 <= maxW16) return at;
  }
  return -1;
}

// Notfall: Wort passt auch allein nicht und hat keine passende Trennstelle.
static int hardBreak(const Font &f, const char *s, int lineStart, int te, int maxW16) {
  const char *p = s + lineStart, *end = s + te;
  int last = -1;
  int hyph16 = textWidth16(f, "-");
  int count = 0;
  while (p < end) {
    utf8Next(p, end);
    count++;
    if (count >= 2 && textWidth16(f, s + lineStart, p - s - lineStart) + hyph16 <= maxW16) last = p - s;
  }
  return last;
}

int lineWidth(const Font &f, const char *s, const TextLine &l) {
  int w = textWidth16(f, s + l.start, l.len);
  if (l.hyphen) w += textWidth16(f, "-");
  if (l.ellipsis) w += textWidth16(f, "\xE2\x80\xA6");
  return (w + 8) / 16;
}

int drawLine(Canvas &c, const Font &f, int x, int baseline, const char *s, const TextLine &l, Color col) {
  int w = drawText(c, f, x, baseline, s + l.start, l.len, col);
  if (l.hyphen) w += drawText(c, f, x + w, baseline, "-", -1, col);
  if (l.ellipsis) w += drawText(c, f, x + w, baseline, "\xE2\x80\xA6", -1, col);
  return w;
}

int wrapText(const Font &f, const char *s, int maxW, TextLine *lines, int maxLines) {
  int total = strlen(s);
  int maxW16 = maxW * 16;
  int n = 0;
  int pos = 0;
  while (pos < total && s[pos] == ' ') pos++;
  while (pos < total && n < maxLines) {
    int lineStart = pos;
    int lineEnd = -1;  // exklusiv
    int nextPos = total;
    bool hyph = false;
    int scan = pos;
    while (true) {
      // naechstes Token: bis Leerzeichen, oder bis nach '-' bzw. '/' innerhalb eines Wortes
      int ts = scan, te = scan;
      while (te < total && s[te] != ' ') {
        if ((s[te] == '-' || s[te] == '/') && te > ts && te + 1 < total && s[te + 1] != ' ') { te++; break; }
        te++;
      }
      if (textWidth16(f, s + lineStart, te - lineStart) <= maxW16) {
        lineEnd = te;
        scan = te;
        while (scan < total && s[scan] == ' ') scan++;
        nextPos = scan;
        if (te >= total) break;
        continue;
      }
      // Token passt nicht mehr: erst Silbentrennung versuchen
      int at = hyphenBreak(f, s, lineStart, ts, te, maxW16);
      if (at > ts) {
        lineEnd = at;
        hyph = s[at - 1] != '-';
        nextPos = at;
      } else if (lineEnd < 0) {
        at = hardBreak(f, s, lineStart, te, maxW16);
        if (at <= lineStart) at = te;  // ueberstehen lassen
        lineEnd = at;
        hyph = at < te && s[at - 1] != '-';
        nextPos = at;
      } else {
        nextPos = ts;
      }
      break;
    }
    int end = lineEnd;
    while (end > lineStart && s[end - 1] == ' ') end--;
    lines[n].start = lineStart;
    lines[n].len = end - lineStart;
    lines[n].hyphen = hyph;
    lines[n].ellipsis = false;
    n++;
    pos = nextPos;
    while (pos < total && s[pos] == ' ') pos++;
  }
  if (pos < total && n > 0) {
    // gekuerzt: letzte Zeile mit "…" so weit verkuerzen, dass es passt
    TextLine &l = lines[n - 1];
    l.hyphen = false;
    l.ellipsis = true;
    while (l.len > 0 && lineWidth(f, s, l) > maxW) {
      int e = l.start + l.len - 1;
      while (e > l.start && ((uint8_t)s[e] & 0xC0) == 0x80) e--;
      l.len = e - l.start;
      while (l.len > 0 && s[l.start + l.len - 1] == ' ') l.len--;
    }
  }
  return n;
}
