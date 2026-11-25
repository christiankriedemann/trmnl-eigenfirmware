#pragma once
#include <Arduino.h>

// Updates per WLAN: Versionsdatei (manifest.json) abrufen, bei neuerer Version die
// Firmware laden, Pruefsumme und Signatur (ECDSA P-256) pruefen, dann einspielen.
// Die neue Version laeuft "auf Probe": meldet sie sich nach dem ersten Lauf nicht als
// gesund (markHealthy), startet der Bootloader beim naechsten Mal die vorige.
namespace ota {

extern const char *DEFAULT_MANIFEST;

struct Result {
  bool checked = false;     // Versionsdatei gelesen
  bool available = false;   // neuere Version gefunden
  bool installed = false;   // eingespielt, Neustart noetig
  String version;
  String error;
};

// Prueft hoechstens einmal je 24 h (force = sofort). Spielt bei Bedarf ein.
Result check(bool force, void (*progress)(const String &version) = nullptr);
// Nach einem erfolgreichen Lauf aufrufen: bestaetigt eine frisch eingespielte Version.
void markHealthy();
// true, wenn die laufende Version noch auf Probe ist
bool pendingVerify();

}  // namespace ota
