#include "layout.h"

#include <stdio.h>
#include <string.h>

#include "fonts_inter.h"
#include "icons.h"
#include "qrcode.h"
#include "text.h"

void modelClear(Model &m) { memset(&m, 0, sizeof(m)); m.batteryPercent = -1; }

// ---------- Masse ----------
// Drei Tagesspalten zwischen x = 16 und 500 (12 px vor der Trennlinie bei 512)
static const int COL_GAP = 16;
static const int COL_W = (484 - 2 * COL_GAP) / 3;  // 150
static const int COL_X[3] = {16, 16 + COL_W + COL_GAP, 16 + 2 * (COL_W + COL_GAP)};
static const int HEADER_TOP = 14;
static const int HEADER_LINE_Y = 104;  // 2 px Linie unter dem Tageskopf
static const int CONTENT_TOP = 120;
static const int ITEM_GAP = 12;
static const int SIDEBAR_LINE_X = 512;
static const int TASK_X = 530;
static const int TASK_W = 254;
static const int TASK_TOP = 14;
static const int TASK_GAP = 13;
static const int RIGHT_EDGE = 780;

static const Icon *iconFor(uint8_t i) {
  switch (i) {
    case WI_SUN: return &icon_sun22;
    case WI_RAIN: return &icon_rain22;
    case WI_SNOW: return &icon_snow22;
    case WI_STORM: return &icon_storm22;
    case WI_CLOUD: return &icon_cloud22;
  }
  return nullptr;
}

// Grundlinie fuer eine Zeile mit gegebener Zeilenhoehe (CSS-Verhalten: Halbdurchschuss)
static int baselineFor(const Font &f, float top, float lineHeight) {
  float content = f.ascent + f.descent;
  return (int)(top + (lineHeight - content) / 2.0f + f.ascent + 0.5f);
}

// ---------- Tageskopf ----------
static void drawDayHeader(Canvas &c, const Day &d, int x) {
  int base = baselineFor(inter34b, HEADER_TOP, 34);
  int w = drawText(c, inter34b, x, base, d.name);
  drawText(c, inter28m, x + w + 6, base, d.date);

  if (d.hasWeather) {
    // Wettertext unten, darueber Symbol und Temperatur (wie margin-top:auto)
    // Passt der Text nicht, zuerst das vordere Wort abkuerzen ("Ueberw. bewoelkt"),
    // damit das Entscheidende lesbar bleibt; erst danach kuerzt der Umbruch mit "…".
    char wt[40];
    snprintf(wt, sizeof(wt), "%s", d.weatherText);
    if (textWidth(inter15b, wt) > COL_W) {
      static const char *LANG[][2] = {{"\xC3\x9C" "berwiegend ", "\xC3\x9C" "berw. "}, {"Teilweise ", "Teilw. "},
                                      {"Leichter ", "Leicht. "}, {"Leichte ", "Leicht. "}, {"Starker ", "Stark. "},
                                      {"Heftige ", "Heft. "}, {"Gefrierender ", "Gefr. "}};
      for (auto &p : LANG) {
        size_t n = strlen(p[0]);
        if (!strncmp(wt, p[0], n)) {
          char rest[40];
          snprintf(rest, sizeof(rest), "%s", wt + n);
          snprintf(wt, sizeof(wt), "%s%s", p[1], rest);
          break;
        }
      }
    }
    TextLine wl[1];
    int n = wrapText(inter15b, wt, COL_W, wl, 1);
    float textBottom = HEADER_LINE_Y - 4 - 1;
    float lh = 15 * 1.15f;
    float textTop = textBottom - n * lh;
    for (int i = 0; i < n; i++) drawLine(c, inter15b, x, baselineFor(inter15b, textTop + i * lh, lh), wt, wl[i]);
    float tempBottom = textTop - 4;
    float tempTop = tempBottom - 26;
    int tb = baselineFor(inter26b, tempTop, 26);
    const Icon *ic = iconFor(d.icon);
    int tx = x;
    if (ic) {
      int iy = (int)(tempBottom + 1 + 0.5f) - ic->h;
      c.blitRows(ic->bits, ic->w, ic->h, x, iy, BLACK);
      tx = x + ic->w + 6;
    }
    char buf[8];
    snprintf(buf, sizeof(buf), "%d\xC2\xB0", d.temp);
    drawText(c, inter26b, tx, tb, buf);
  }
  c.fillRect(x, HEADER_LINE_Y, COL_W, 2, BLACK);
}

// ---------- Tagesinhalt ----------
// Liefert die Hoehe, die ein Eintrag braucht (ohne Abstand), und zeichnet ihn, wenn draw = true.
static int pillBlock(Canvas &c, const Event &e, int x, int y, bool draw) {
  TextLine l[3];
  int n = wrapText(inter14b, e.title, COL_W - 16, l, 3);
  float lh = 14 * 1.1f;
  int h = (int)(n * lh + 8 + 0.5f);
  if (draw) {
    int w = 0;
    for (int i = 0; i < n; i++) { int lw = lineWidth(inter14b, e.title, l[i]); if (lw > w) w = lw; }
    w += 16;
    if (w > COL_W) w = COL_W;
    c.fillRoundRect(x, y, w, h, 2, BLACK);
    for (int i = 0; i < n; i++) drawLine(c, inter14b, x + 8, baselineFor(inter14b, y + 4 + i * lh, lh), e.title, l[i], WHITE);
  }
  return h + 2;  // margin-bottom 2
}

static int eventBlock(Canvas &c, const Event &e, int x, int y, bool draw) {
  TextLine l[4];
  int n = wrapText(inter17m, e.title, COL_W - 10, l, 4);
  float timeLh = 18;
  float titleLh = 17 * 1.2f;
  int h = (int)(timeLh + n * titleLh + 0.5f);
  if (draw) {
    c.fillRect(x, y, 5, h, BLACK);
    drawText(c, inter15b, x + 10, baselineFor(inter15b, y, timeLh), e.time);
    for (int i = 0; i < n; i++)
      drawLine(c, inter17m, x + 10, baselineFor(inter17m, y + timeLh + i * titleLh, titleLh), e.title, l[i]);
  }
  return h;
}

// Kompakter Termin: nur das Zeitfenster, fuer volle Tage
static const int COMPACT_H = 18;
static const int COMPACT_GAP = 6;

static int compactBlock(Canvas &c, const Event &e, int x, int y, bool draw) {
  if (draw) {
    c.fillRect(x, y, 5, COMPACT_H, BLACK);
    drawText(c, inter15b, x + 10, baselineFor(inter15b, y, COMPACT_H), e.time);
  }
  return COMPACT_H;
}

// Passen nicht alle Termine voll, stehen die naechsten (bis zu 2) vollstaendig da und die
// uebrigen nur mit Zeitfenster. Reicht auch das nicht, folgt "+N weitere".
static void drawDayContent(Canvas &c, const Day &d, int x, int bottom) {
  int avail = bottom - CONTENT_TOP;
  int pillsH = 0;
  for (int i = 0; i < d.nAllDay; i++) pillsH += pillBlock(c, d.allDay[i], x, 0, false) + ITEM_GAP;
  int fullH[MAX_TIMED];
  int allFull = pillsH;
  for (int i = 0; i < d.nTimed; i++) { fullH[i] = eventBlock(c, d.timed[i], x, 0, false); allFull += fullH[i] + ITEM_GAP; }

  int nFull = d.nTimed;  // so viele Termine voll, der Rest kompakt
  if (allFull - ITEM_GAP > avail && d.nTimed > 0) {
    nFull = 0;
    for (int k = (d.nTimed < 2 ? d.nTimed : 2); k >= 0; k--) {
      int h = pillsH;
      for (int i = 0; i < k; i++) h += fullH[i] + ITEM_GAP;
      h += (d.nTimed - k) * (COMPACT_H + COMPACT_GAP);
      if (h - COMPACT_GAP <= avail) { nFull = k; break; }
    }
  }

  int y = CONTENT_TOP;
  int total = d.nAllDay + d.nTimed;
  int shown = 0;
  for (int i = 0; i < total; i++) {
    bool pill = i < d.nAllDay;
    int t = i - d.nAllDay;
    const Event &e = pill ? d.allDay[i] : d.timed[t];
    bool compact = !pill && t >= nFull;
    int h = pill ? pillBlock(c, e, x, y, false) : (compact ? COMPACT_H : fullH[t]);
    int gap = compact ? COMPACT_GAP : ITEM_GAP;
    int reserve = (total - i - 1) > 0 ? 20 : 0;  // Platz fuer "+N weitere"
    if (y + h > bottom - reserve) break;
    if (pill) pillBlock(c, e, x, y, true);
    else if (compact) compactBlock(c, e, x, y, true);
    else eventBlock(c, e, x, y, true);
    y += h + gap;
    shown++;
  }
  if (shown < total) {
    char buf[24];
    snprintf(buf, sizeof(buf), "+%d weitere", total - shown);
    int base = y + 12;
    if (base > bottom - 2) base = bottom - 2;
    drawText(c, inter14b, x, base, buf);
  }
}

// ---------- Aufgaben ----------
static void drawTasks(Canvas &c, const Model &m, int bottom) {
  float lh = 18 * 1.2f;
  float y = TASK_TOP;
  for (int t = 0; t < m.nTasks; t++) {
    const Task &k = m.tasks[t];
    TextLine l[4];
    int n = wrapText(inter18m, k.title, TASK_W, l, 4);
    if (n == 0) continue;  // leerer Titel
    char due[24] = "";
    int dueW = 0;
    if (k.due[0]) {
      snprintf(due, sizeof(due), "(%s)", k.due);
      dueW = textWidth(inter14b, due);
    }
    bool dueOwnLine = false;
    if (dueW && lineWidth(inter18m, k.title, l[n - 1]) + 5 + dueW > TASK_W) dueOwnLine = true;
    int lines = n + (dueOwnLine ? 1 : 0);
    float h = lines * lh;
    if (y + h > bottom) break;
    for (int i = 0; i < n; i++) {
      int base = baselineFor(inter18m, y + i * lh, lh);
      int w = drawLine(c, inter18m, TASK_X, base, k.title, l[i]);
      if (i == n - 1 && dueW && !dueOwnLine) drawText(c, inter14b, TASK_X + w + 5, base, due);
    }
    if (dueOwnLine) drawText(c, inter14b, TASK_X + 5, baselineFor(inter18m, y + n * lh, lh), due);
    y += h + TASK_GAP;
  }
}

// Akku links in der Fusszeile der Aufgabenspalte, nur wenn kritisch
static void drawBattery(Canvas &c, const Model &m, int baseline) {
  if (!m.batteryCritical || m.batteryPercent < 0) return;
  int x = TASK_X, y = baseline - 11;
  c.rect(x, y, 17, 11, 2, BLACK);
  c.fillRect(x + 17, y + 3, 2, 5, BLACK);
  int fill = m.batteryPercent * 11 / 100;
  if (fill < 1) fill = 1;
  c.fillRect(x + 3, y + 3, fill, 5, BLACK);
  char buf[8];
  snprintf(buf, sizeof(buf), "%d%%", m.batteryPercent);
  drawText(c, inter13b, x + 25, baseline, buf);
}

static const int BANNER_H = 32;  // Hoehe des Hinweisbalkens oberhalb von safeBottom

static void drawBanner(Canvas &c, const char *text, const LayoutOptions &o, bool show) {
  if (!show) return;
  int top = o.safeBottom - BANNER_H;
  c.fillRect(0, top, CANVAS_W, CANVAS_H - top, BLACK);
  TextLine l[1];
  wrapText(inter18b, text, CANVAS_W - 32, l, 1);
  int w = lineWidth(inter18b, text, l[0]);
  drawLine(c, inter18b, (CANVAS_W - w) / 2, baselineFor(inter18b, top, BANNER_H), text, l[0], WHITE);
}

void renderDashboard(Canvas &c, const Model &m, const LayoutOptions &o) {
  c.clear(WHITE);
  bool banner = m.banner[0] != 0;
  int contentBottom = banner ? o.safeBottom - BANNER_H - 6 : o.safeBottom;

  for (int i = 0; i < 3; i++) {
    drawDayHeader(c, m.days[i], COL_X[i]);
    c.setClip(COL_X[i], 0, COL_X[i] + COL_W + 1, contentBottom);
    drawDayContent(c, m.days[i], COL_X[i], contentBottom);
    c.resetClip();
  }

  // Trennlinie zur Aufgabenspalte, ueber die volle Hoehe
  c.fillRect(SIDEBAR_LINE_X, 0, 2, CANVAS_H, BLACK);

  // Fusszeile rechts unten: "Stand: 14:43", fern der Heimat mit Ort davor
  char foot[80];
  if (m.place[0]) snprintf(foot, sizeof(foot), "%s \xC2\xB7 Stand: %s", m.place, m.stand);
  else snprintf(foot, sizeof(foot), "Stand: %s", m.stand);
  int footBase = contentBottom - 4;
  int taskBottom = footBase - 13 - 8;
  c.setClip(TASK_X, 0, RIGHT_EDGE + 4, taskBottom);
  drawTasks(c, m, taskBottom);
  c.resetClip();
  if (!banner) {
    drawTextRight(c, inter13b, RIGHT_EDGE, footBase, foot);
    drawBattery(c, m, footBase);
    drawBanner(c, "", o, false);
  } else {
    char text[160];
    if (m.batteryCritical && m.batteryPercent >= 0 && !strstr(m.banner, "Akku")) snprintf(text, sizeof(text), "%s \xC2\xB7 Akku %d%%", m.banner, m.batteryPercent);
    else snprintf(text, sizeof(text), "%s", m.banner);
    drawBanner(c, text, o, true);
  }
}

// ---------- QR- und Textbildschirme ----------

static void drawParagraphs(Canvas &c, const char *body, int x, int y, int w, int bottom) {
  // Absaetze an "\n" trennen
  char para[400];
  const char *p = body;
  float lh = 18 * 1.35f;
  float yy = y;
  while (*p && yy < bottom) {
    const char *nl = strchr(p, '\n');
    int len = nl ? nl - p : strlen(p);
    if (len >= (int)sizeof(para)) len = sizeof(para) - 1;
    memcpy(para, p, len);
    para[len] = 0;
    if (len == 0) {
      yy += lh * 0.5f;
    } else {
      TextLine l[8];
      int n = wrapText(inter18m, para, w, l, 8);
      for (int i = 0; i < n && yy < bottom; i++) {
        drawLine(c, inter18m, x, baselineFor(inter18m, yy, lh), para, l[i]);
        yy += lh;
      }
    }
    p = nl ? nl + 1 : p + len;
  }
}

void renderQrScreen(Canvas &c, const char *qrText, const char *title, const char *body, const char *footer,
                    const LayoutOptions &o) {
  c.clear(WHITE);
  // QR-Code links, so gross wie moeglich, mit Ruhezone
  static uint8_t qrData[512];  // reicht bis Version 10 (407 Byte)
  QRCode qr;
  int ver = 0;
  for (int v = 3; v <= 10; v++) {
    if (qrcode_initText(&qr, qrData, v, ECC_MEDIUM, qrText) == 0) { ver = v; break; }
  }
  int areaTop = 24, areaBottom = o.safeBottom - 8;
  int areaH = areaBottom - areaTop;
  if (ver) {
    int scale = (areaH - 16) / qr.size;
    if (scale > 8) scale = 8;
    int size = qr.size * scale;
    int qx = 32, qy = areaTop + (areaH - size) / 2;
    for (int yy = 0; yy < qr.size; yy++)
      for (int xx = 0; xx < qr.size; xx++)
        if (qrcode_getModule(&qr, xx, yy)) c.fillRect(qx + xx * scale, qy + yy * scale, scale, scale, BLACK);
  }
  int qrRight = ver ? 32 + qr.size * ((areaH - 16) / qr.size > 8 ? 8 : (areaH - 16) / qr.size) : 32;
  int tx = qrRight + 40;
  int tw = CANVAS_W - tx - 24;
  TextLine l[3];
  int n = wrapText(inter28m, title, tw, l, 3);
  int y = 40;
  for (int i = 0; i < n; i++) { drawLine(c, inter28m, tx, y + 26, title, l[i]); y += 34; }
  y += 12;
  drawParagraphs(c, body, tx, y, tw, areaBottom - 30);
  if (footer && *footer) {
    TextLine f[2];
    int fn = wrapText(inter14b, footer, tw, f, 2);
    for (int i = 0; i < fn; i++) drawLine(c, inter14b, tx, areaBottom - 6 - (fn - 1 - i) * 18, footer, f[i]);
  }
}

void renderLoginScreen(Canvas &c, const char *qrText, const char *title, const char *code, const char *body,
                       const char *footer, const LayoutOptions &o) {
  renderQrScreen(c, qrText, title, "", footer, o);
  static uint8_t qrData[512];
  QRCode qr;
  int ver = 0;
  for (int v = 3; v <= 10; v++)
    if (qrcode_initText(&qr, qrData, v, ECC_MEDIUM, qrText) == 0) { ver = v; break; }
  int areaH = o.safeBottom - 8 - 24;
  int scale = ver ? (areaH - 16) / qr.size : 0;
  if (scale > 8) scale = 8;
  int tx = (ver ? 32 + qr.size * scale : 32) + 40;
  int tw = CANVAS_W - tx - 24;
  TextLine l[3];
  int n = wrapText(inter28m, title, tw, l, 3);
  int y = 40 + n * 34 + 16;
  drawText(c, inter34b, tx, y + 34, code);
  drawParagraphs(c, body, tx, y + 34 + 24, tw, o.safeBottom - 40);
}

void renderMessage(Canvas &c, const char *title, const char *body, const LayoutOptions &o) {
  c.clear(WHITE);
  TextLine l[3];
  int n = wrapText(inter34b, title, CANVAS_W - 96, l, 3);
  int y = 60;
  for (int i = 0; i < n; i++) { drawLine(c, inter34b, 48, y + 34, title, l[i]); y += 42; }
  y += 20;
  drawParagraphs(c, body, 48, y, CANVAS_W - 96, o.safeBottom - 8);
}
