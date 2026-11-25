#pragma once
#include <stdint.h>

// Liefert in breaks[i] = 1, wenn vor Zeichen i getrennt werden darf (deutsch, neue Rechtschreibung).
// cps: Codepunkte eines Wortes ohne Satzzeichen. Rueckgabe: Anzahl Trennstellen.
int hyphenate(const uint32_t *cps, int n, uint8_t *breaks, int leftMin = 2, int rightMin = 3);
