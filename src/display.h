#pragma once
#include <stdint.h>

#include "canvas.h"

// Ansteuerung des 7,5"-Panels ueber bb_epaper (Pins des TRMNL OG)
namespace display {
uint8_t *buffer();  // 48 000 Byte, Format wie Canvas
void show(bool full = true);
void sleep();
}  // namespace display
