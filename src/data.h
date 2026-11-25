#pragma once
#include <stdint.h>
#include <time.h>

#include "model.h"

// Rohdaten, wie sie von den Quellen kommen. Werden im Flash zwischengespeichert,
// damit das Geraet ohne WLAN aus den letzten Daten neu zeichnen kann
// (vergangene Termine fallen raus, "heute" rueckt weiter).

static const int MAX_RAW_EVENTS = 64;
static const int MAX_RAW_TASKS = 48;
static const int WEATHER_HOURS = 96;

struct RawEvent {
  char title[112];
  bool allDay;
  int32_t startDay, endDay;  // ganztaegig: Kalendertage, Ende exklusiv
  int64_t start, end;        // mit Uhrzeit: UTC
};

struct RawTask {
  char title[144];
  char id[24];
  int32_t dueDay;       // -1 = ohne
  int32_t deadlineDay;  // -1 = ohne
  int8_t prio;          // 1 hoch, 2 mittel, 3 niedrig/keine
  int32_t addedDay;     // fuer die Reihenfolge innerhalb einer Stufe
  int32_t order;
  uint8_t src;          // TASK_SRC_*: bestimmt die Beschriftung der Prioritaet
};

enum : uint8_t { TASK_SRC_TODOIST = 0, TASK_SRC_GOOGLE = 1, TASK_SRC_MICROSOFT = 2 };

struct RawData {
  uint32_t magic;
  int64_t fetchedAt;  // letzte erfolgreiche Aktualisierung (UTC)
  RawEvent events[MAX_RAW_EVENTS];
  uint16_t nEvents;
  RawTask tasks[MAX_RAW_TASKS];
  uint16_t nTasks;
  // Wetter: stuendlich ab weatherStart (UTC)
  int64_t weatherStart;
  int8_t temp[WEATHER_HOURS];
  uint8_t code[WEATHER_HOURS];
  uint8_t weatherHours;
  // Ort
  char tz[40];         // IANA
  int32_t tzOffset;    // Rueckfall, falls die Zone unbekannt ist
  char place[40];
  bool calendarOk, tasksOk, weatherOk;
};

static const uint32_t RAW_MAGIC = 0x45494732;  // "EIG2" (bei Aenderung der Struktur hochzaehlen)

struct TaskQuotas {
  int slots = 7, overdue = 4, due = 2, deadline = 2, undated = 1;
};

// Baut das Anzeigemodell fuer den Zeitpunkt now in der aktuell gesetzten Zeitzone.
void buildModel(const RawData &raw, time_t now, const TaskQuotas &q, Model &m);
const char *weatherText(uint8_t wmo);
uint8_t weatherIcon(uint8_t wmo);
uint32_t modelHash(const Model &m);
