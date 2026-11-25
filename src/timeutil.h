#pragma once
#include <stdint.h>
#include <time.h>

// Zeitzone und Datumsrechnung. Intern laeuft alles in UTC (time_t),
// fuer die Anzeige wird per POSIX-TZ-Regel in lokale Zeit umgerechnet.
namespace tu {

// Setzt die lokale Zeitzone ueber den IANA-Namen. Liefert false, wenn unbekannt
// (dann gilt UTC + Offset als Rueckfall).
bool setZone(const char *iana, int fallbackOffsetSec = 0);
const char *posixFor(const char *iana);

// Tage seit 1970-01-01 fuer ein Kalenderdatum (proleptisch gregorianisch)
int32_t daysFromCivil(int y, int m, int d);
void civilFromDays(int32_t z, int &y, int &m, int &d);

// Lokaler Kalendertag eines Zeitpunkts als Tageszahl
int32_t localDay(time_t t);
// Lokale Mitternacht eines Kalendertages als UTC-Zeitpunkt
time_t localMidnight(int32_t day);
// Offset der lokalen Zeit zu UTC in Sekunden zum Zeitpunkt t
int32_t utcOffset(time_t t);

// "2026-11-26T10:00:00+01:00", "...Z" oder "...T10:00:00" (dann UTC) -> time_t; 0 bei Fehler
time_t parseRfc3339(const char *s);
// "2026-11-26" (auch laengere Strings, nur die ersten 10 Zeichen) -> Tageszahl, -1 bei Fehler
int32_t parseDate(const char *s);
// time_t -> "2026-11-26T09:00:00Z"
void formatUtc(time_t t, char *out, int n);

}  // namespace tu
