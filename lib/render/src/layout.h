#pragma once
#include "canvas.h"
#include "model.h"

// Masse in Pixel (natives 800 x 480), abgeleitet aus dem bisherigen n8n-Layout
// (1600er Design mit zoom 0.5).
struct LayoutOptions {
  // Unterer Rand: Der Rahmen des Geraets deckt unten mehr ab als oben.
  // Nichts Wichtiges unterhalb dieser Zeile zeichnen.
  int safeBottom = 448;
};

void renderDashboard(Canvas &c, const Model &m, const LayoutOptions &o = LayoutOptions());

// Bildschirm mit QR-Code links und Text rechts (Einrichtung, Anmeldung).
// lines: Absaetze, "\n"-getrennt; die erste Zeile wird als Ueberschrift gesetzt.
void renderQrScreen(Canvas &c, const char *qrText, const char *title, const char *body, const char *footer,
                    const LayoutOptions &o = LayoutOptions());

// Anmeldung per Geraetecode: QR links, darunter rechts der Code gross
void renderLoginScreen(Canvas &c, const char *qrText, const char *title, const char *code, const char *body,
                       const char *footer, const LayoutOptions &o = LayoutOptions());

// Einfacher Textbildschirm (Fehler, Hinweise)
void renderMessage(Canvas &c, const char *title, const char *body, const LayoutOptions &o = LayoutOptions());
