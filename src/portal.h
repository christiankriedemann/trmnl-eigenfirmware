#pragma once
#include <Arduino.h>

// Einrichtungsmodus: eigenes WLAN (WPA2), Captive Portal, Webinterface.
namespace portal {

struct Result {
  bool changed;  // Einstellungen gespeichert
};

// Laeuft, bis niemand mehr verbunden ist und das Zeitfenster abgelaufen ist.
// firstMs: Fenster ohne Verbindung; afterSaveMs: Nachlauf nach dem letzten Speichern.
Result run(uint32_t firstMs, uint32_t afterSaveMs, bool keepStaConnection);

String apSsid();
String apPassword();

}  // namespace portal
