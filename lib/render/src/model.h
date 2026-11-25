#pragma once
#include <stdint.h>

// Alles, was auf dem Dashboard steht. Texte in UTF-8, Zeiten schon fuer die
// lokale Zeitzone formatiert. Feste Groessen, damit der Speicherbedarf auf dem
// ESP32-C3 vorhersehbar bleibt.

static const int MAX_ALLDAY = 6;
static const int MAX_TIMED = 12;
static const int MAX_TASKS = 12;

enum WeatherIcon : uint8_t { WI_NONE = 0, WI_SUN, WI_CLOUD, WI_RAIN, WI_SNOW, WI_STORM };

struct Event {
  char title[112];
  char time[24];     // "10:00 – 10:30"
  int32_t sortKey;   // Minuten seit Tagesbeginn (lokal), fuer die Reihenfolge
};

struct Day {
  char name[4];      // "MI"
  char date[8];      // "25.11."
  Event allDay[MAX_ALLDAY];
  uint8_t nAllDay;
  Event timed[MAX_TIMED];
  uint8_t nTimed;
  bool hasWeather;
  int8_t temp;
  uint8_t icon;      // WeatherIcon
  char weatherText[32];
};

struct Task {
  char title[144];
  char due[16];      // "23.7." (ohne Stichwort, wie bisher)
};

struct Model {
  Day days[3];
  Task tasks[MAX_TASKS];
  uint8_t nTasks;
  int8_t batteryPercent;   // -1 = unbekannt
  bool batteryCritical;
  char stand[8];           // "14:43", Zeitpunkt der letzten erfolgreichen Aktualisierung
  char place[40];          // nur fern der Heimatzeitzone: "Bangkok, UTC+7"
  // Hinweisbalken unten (leer = kein Balken), z. B. "Kein WLAN · letzter Sync 25.11. 13:50"
  char banner[112];
};

void modelClear(Model &m);
