// Deutsche Silbentrennung nach Liang (TeX-Muster hyph-de-1996)
#include "hyph.h"

#include <string.h>

#include "hyph_de.h"

static uint8_t codeFor(uint32_t cp) {
  if (cp >= 'A' && cp <= 'Z') cp += 32;
  if (cp >= 0xC0 && cp <= 0xDE && cp != 0xD7) cp += 32;  // Latin-1-Grossbuchstaben
  for (int i = 0; i < hyph_alphabet_len; i++)
    if (hyph_alphabet[i] == cp) return i + 1;
  return 0;
}

static int cmpKey(const uint8_t *a, int na, const uint8_t *b, int nb) {
  int n = na < nb ? na : nb;
  for (int i = 0; i < n; i++)
    if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
  return na == nb ? 0 : (na < nb ? -1 : 1);
}

// Sucht das Muster key und traegt seine Werte ab Position at in vals ein.
static void applyPattern(const uint8_t *key, int n, uint8_t *vals, int at) {
  // Block per Binaersuche ueber die Blockkoepfe finden
  int lo = 0, hi = hyph_nblocks - 1, blk = -1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    const uint8_t *p = hyph_data + hyph_blocks[mid];
    int suf = p[0] & 0x0F;
    int c = cmpKey(p + 1, suf, key, n);
    if (c <= 0) { blk = mid; lo = mid + 1; } else hi = mid - 1;
  }
  if (blk < 0) return;
  const uint8_t *p = hyph_data + hyph_blocks[blk];
  uint8_t cur[16];
  int curLen = 0;
  for (int i = 0; i < hyph_block && blk * hyph_block + i < hyph_count; i++) {
    int pre = p[0] >> 4, suf = p[0] & 0x0F;
    memcpy(cur + pre, p + 1, suf);
    curLen = pre + suf;
    p += 1 + suf;
    int nv = *p++;
    int c = cmpKey(cur, curLen, key, n);
    if (c == 0) {
      for (int k = 0; k < nv; k++) {
        int pos = p[k] >> 4, v = p[k] & 0x0F;
        if (vals[at + pos] < v) vals[at + pos] = v;
      }
      return;
    }
    if (c > 0) return;
    p += nv;
  }
}

int hyphenate(const uint32_t *cps, int n, uint8_t *breaks, int leftMin, int rightMin) {
  memset(breaks, 0, n + 1);
  if (n < leftMin + rightMin || n > 60) return 0;
  uint8_t word[64];
  word[0] = codeFor('.');
  for (int i = 0; i < n; i++) {
    word[i + 1] = codeFor(cps[i]);
    if (!word[i + 1]) return 0;  // unbekanntes Zeichen: nicht trennen
  }
  word[n + 1] = codeFor('.');
  int len = n + 2;
  uint8_t vals[66];
  memset(vals, 0, sizeof(vals));
  for (int s = 0; s < len; s++)
    for (int l = 1; l <= 12 && s + l <= len; l++) applyPattern(word + s, l, vals, s);
  int count = 0;
  // vals[k] gehoert zur Luecke vor word[k]; Wortzeichen i ist word[i+1]
  for (int i = leftMin; i <= n - rightMin; i++) {
    if (vals[i + 1] & 1) { breaks[i] = 1; count++; }
  }
  return count;
}
