#include "display.h"

#include <Arduino.h>
#include <bb_epaper.h>

// Pins TRMNL OG (aus der Original-Firmware, board_config.cpp "og")
static const int PIN_SCK = 7, PIN_MOSI = 8, PIN_CS = 6, PIN_RST = 10, PIN_DC = 5, PIN_BUSY = 4;

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

void show(bool full) {
  init();
  bbep.writePlane(PLANE_0);
  bbep.refresh(full ? REFRESH_FULL : REFRESH_FAST, true);
}

void sleep() {
  if (!ready) return;
  bbep.sleep(DEEP_SLEEP);
}

}  // namespace display
