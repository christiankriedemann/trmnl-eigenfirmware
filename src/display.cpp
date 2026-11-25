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

namespace display {

uint8_t *buffer() { return frame; }

static void init() {
  if (ready) return;
  bbep.setPanelType(EP75_800x480);
  bbep.initIO(PIN_DC, PIN_RST, PIN_BUSY, PIN_CS, PIN_MOSI, PIN_SCK, 8000000);
  bbep.setMemoryMode(BBEP_MODE_1BPP);
  bbep.setBuffer(frame);
  ready = true;
}

static void remember() {
  File f = LittleFS.open(LAST_FILE, "w");
  if (!f) return;
  f.write(frame, CANVAS_BYTES);
  f.close();
}

bool show(bool full) {
  init();
  bool partial = !full;
  uint8_t *old = nullptr;
  if (partial) {
    // Der Controller vergisst im Tiefschlaf das alte Bild: aus dem Flash nachladen
    old = (uint8_t *)malloc(CANVAS_BYTES);
    File f = LittleFS.open(LAST_FILE, "r");
    partial = old && f && f.read(old, CANVAS_BYTES) == CANVAS_BYTES;
    if (f) f.close();
  }
  if (partial) {
    bbep.setBuffer(old);
    bbep.writePlane(PLANE_1);  // "altes" Bild
    bbep.setBuffer(frame);
    bbep.writePlane(PLANE_0);  // neues Bild
    bbep.refresh(REFRESH_PARTIAL, true);
  } else {
    bbep.setBuffer(frame);
    bbep.writePlane(PLANE_0);
    bbep.refresh(REFRESH_FULL, true);
  }
  free(old);
  remember();
  return partial;
}

void sleep() {
  if (!ready) return;
  bbep.sleep(DEEP_SLEEP);
}

}  // namespace display
