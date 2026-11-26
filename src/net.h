#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

namespace net {

// Verbindet mit dem staerksten bekannten WLAN. Liefert die SSID oder "" bei Fehlschlag.
String connect(uint32_t timeoutMs = 15000);
bool syncTime(uint32_t timeoutMs = 8000);
String syncInfo();  // Ergebnis des Zeitabgleichs dieser Runde, leer ohne Versuch
void off();

// HTTPS-Anfragen (Zertifikate aus dem eingebetteten Buendel). Antwort wird
// gestreamt und mit filter in doc gelesen. Liefert den HTTP-Status (<0 = Netzfehler).
int getJson(const String &url, JsonDocument &doc, const JsonDocument *filter = nullptr,
            const char *bearer = nullptr, const char *extraName = nullptr, const char *extraValue = nullptr);
int postJson(const String &url, const String &body, const char *contentType, JsonDocument &doc,
             const JsonDocument *filter = nullptr, const char *bearer = nullptr);

String urlEncode(const String &s);
extern const char *USER_AGENT;

}  // namespace net
