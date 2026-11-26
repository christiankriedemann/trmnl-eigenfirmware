#include "display.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <bb_epaper.h>

// Pins TRMNL OG (aus der Original-Firmware, board_config.cpp "og")
static const int PIN_SCK = 7, PIN_MOSI = 8, PIN_CS = 6, PIN_RST = 10, PIN_DC = 5, PIN_BUSY = 4;
static const char *LAST_FILE = "/last.bin";  // zuletzt gezeigtes Bild, Grundlage der Teilaktualisierung

static BBEPAPER bbep(EP75_800x480);
static uint8_t frame[CANVAS_BYTES];
static bool ready = false;
static int32_t changedPx = -1;

namespace display {

uint8_t *buffer() { return frame; }
int32_t lastChanged() { return changedPx; }

static void init() {
  if (ready) return;
  bbep.setPanelType(EP75_800x480);
  bbep.initIO(PIN_DC, PIN_RST, PIN_BUSY, PIN_CS, PIN_MOSI, PIN_SCK, 8000000);
  bbep.setMemoryMode(BBEP_MODE_1BPP);
  bbep.setBuffer(frame);
  ready = true;
}

static const char *NEXT_FILE = "/next.bin";

static bool save(const char *path) {
  File f = LittleFS.open(path, "w");
  if (!f) return false;
  bool ok = f.write(frame, CANVAS_BYTES) == CANVAS_BYTES;
  f.close();
  return ok;
}

static bool load(const char *path) {
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  bool ok = f.read(frame, CANVAS_BYTES) == CANVAS_BYTES;
  f.close();
  return ok;
}

// Geaenderte Bildpunkte zwischen dem Bild im Flash und dem neuen Bild, stueckweise gelesen
// (ein zweiter 48-KB-Puffer ist nach den TLS-Abrufen oft nicht mehr am Stueck frei).
static int32_t countChanged() {
  File f = LittleFS.open(LAST_FILE, "r");
  if (!f || f.size() != CANVAS_BYTES) {
    if (f) f.close();
    return -1;
  }
  static uint8_t chunk[1024];
  uint32_t changed = 0;
  for (int pos = 0; pos < CANVAS_BYTES;) {
    int n = f.read(chunk, sizeof(chunk));
    if (n <= 0) {
      f.close();
      return -1;
    }
    for (int i = 0; i < n; i++) changed += __builtin_popcount((uint8_t)(chunk[i] ^ frame[pos + i]));
    pos += n;
  }
  f.close();
  return (int32_t)changed;
}

bool show(bool full) {
  init();
  bool partial = !full;
  changedPx = -1;
  if (partial) {
    // Der Controller vergisst im Tiefschlaf das alte Bild: mit dem Bild im Flash vergleichen.
    changedPx = countChanged();
    // Grosse Flaechen (z. B. der schwarze Hinweisbalken) werden bei der Teilaktualisierung
    // nur grau und streifig: ab 8 % geaenderter Bildpunkte gruendlich aktualisieren.
    partial = changedPx >= 0 && changedPx <= (int32_t)CANVAS_W * CANVAS_H * 8 / 100;
  }
  if (partial) {
    // Ohne zweiten Puffer: neues Bild senden und sichern, dann das alte in denselben Puffer
    // laden und als Vergleichsbild senden, zum Schluss das neue zurueckholen.
    partial = save(NEXT_FILE);
    if (partial) {
      bbep.setBuffer(frame);
      bbep.writePlane(PLANE_0);  // neues Bild
      if (load(LAST_FILE)) {
        // PLANE_1 liest ab Puffer + 48 KB (zweite Ebene), PLANE_0_TO_1 sendet den Puffer
        // von vorn in den Speicher fuer das alte Bild
        bbep.writePlane(PLANE_0_TO_1);  // "altes" Bild
        bbep.refresh(REFRESH_PARTIAL, true);
      } else {
        partial = false;
      }
      load(NEXT_FILE);
      LittleFS.remove(LAST_FILE);
      LittleFS.rename(NEXT_FILE, LAST_FILE);
    }
    if (partial) return true;
  }
  bbep.setBuffer(frame);
  bbep.writePlane(PLANE_0);
  bbep.refresh(REFRESH_FULL, true);
  save(LAST_FILE);
  return false;
}

void sleep() {
  if (!ready) return;
  bbep.sleep(DEEP_SLEEP);
}

}  // namespace display
