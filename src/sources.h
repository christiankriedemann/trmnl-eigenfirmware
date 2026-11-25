#pragma once
#include <Arduino.h>

#include "data.h"

namespace src {

// Liefert true, wenn die Quelle konfiguriert ist und erfolgreich gelesen wurde.
bool googleCalendar(RawData &raw, time_t from, time_t to, String &err);
bool googleTasks(RawData &raw, String &err);
bool todoist(RawData &raw, String &err);
bool weather(RawData &raw, double lat, double lon, String &err);

// Standort: WLAN-Ortung (BeaconDB), Rueckfall IP. Setzt lat/lon und ggf. tz.
bool locate(double &lat, double &lon, String &tz, String &err);
bool placeName(double lat, double lon, String &name);

// Kalenderliste fuer das Webinterface (JSON-Array mit id und summary)
bool googleCalendarList(String &jsonOut, String &err);

}  // namespace src
