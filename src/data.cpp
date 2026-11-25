#include "data.h"

#include <stdio.h>
#include <string.h>

#include "timeutil.h"

static const char *WD[] = {"SO", "MO", "DI", "MI", "DO", "FR", "SA"};

const char *weatherText(uint8_t c) {
  switch (c) {
    case 0: return "Klarer Himmel";
    case 1: return "\xC3\x9C" "berwiegend klar";
    case 2: return "Teilweise bew\xC3\xB6lkt";
    case 3: return "Bedeckt";
    case 45: case 48: return "Nebel";
    case 51: return "Leichter Nieselregen";
    case 53: return "Nieselregen";
    case 55: return "Starker Nieselregen";
    case 56: case 57: return "Gefrierender Niesel";
    case 61: return "Leichter Regen";
    case 63: return "Regen";
    case 65: return "Starker Regen";
    case 66: case 67: return "Gefrierender Regen";
    case 71: return "Leichter Schneefall";
    case 73: return "Schneefall";
    case 75: return "Starker Schneefall";
    case 77: return "Schneegriesel";
    case 80: return "Leichte Regenschauer";
    case 81: return "Regenschauer";
    case 82: return "Heftige Regenschauer";
    case 85: case 86: return "Schneeschauer";
    case 95: return "Gewitter";
    case 96: case 99: return "Gewitter mit Hagel";
  }
  return "";
}

uint8_t weatherIcon(uint8_t c) {
  if (c <= 1) return WI_SUN;
  if (c <= 48) return WI_CLOUD;
  if ((c >= 71 && c <= 77) || c == 85 || c == 86) return WI_SNOW;
  if (c >= 95) return WI_STORM;
  return WI_RAIN;
}

static void copyStr(char *dst, size_t n, const char *src) { snprintf(dst, n, "%s", src); }

static void hm(time_t t, char *out, int n) {
  struct tm lt;
  localtime_r(&t, &lt);
  snprintf(out, n, "%02d:%02d", lt.tm_hour, lt.tm_min);
}

static void dayMonth(int32_t day, char *out, int n) {
  int y, mo, d;
  tu::civilFromDays(day, y, mo, d);
  snprintf(out, n, "%d.%d.", d, mo);
}

// ---------- Aufgaben: 9 Stufen + Quoten (wie im n8n-Workflow) ----------
struct Ranked {
  int idx, tier;
  int32_t dateKey;
};

static int tierOf(const RawTask &t, int32_t today, int32_t &dateKey) {
  if (t.dueDay >= 0) {
    dateKey = t.dueDay;
    return t.dueDay < today ? 1 : (t.dueDay == today ? 2 : 3);
  }
  if (t.deadlineDay >= 0) {
    dateKey = t.deadlineDay;
    return t.deadlineDay < today ? 4 : (t.deadlineDay == today ? 5 : 6);
  }
  dateKey = t.addedDay >= 0 ? t.addedDay : 999999;
  return t.prio == 1 ? 7 : (t.prio == 2 ? 8 : 9);
}

static int bucketOf(int tier) {
  if (tier == 1) return 0;
  if (tier <= 3) return 1;
  if (tier <= 6) return 2;
  return 3;
}

static bool rankLess(const Ranked &a, const Ranked &b, const RawData &raw) {
  if (a.tier != b.tier) return a.tier < b.tier;
  if (a.dateKey != b.dateKey) return a.dateKey < b.dateKey;
  if (raw.tasks[a.idx].prio != raw.tasks[b.idx].prio) return raw.tasks[a.idx].prio < raw.tasks[b.idx].prio;
  return raw.tasks[a.idx].order < raw.tasks[b.idx].order;
}

static void sortRanked(Ranked *r, int n, const RawData &raw) {
  for (int i = 1; i < n; i++) {
    Ranked x = r[i];
    int j = i - 1;
    while (j >= 0 && rankLess(x, r[j], raw)) { r[j + 1] = r[j]; j--; }
    r[j + 1] = x;
  }
}

static void pickTasks(const RawData &raw, int32_t today, const TaskQuotas &q, Model &m) {
  static Ranked all[MAX_RAW_TASKS];
  int n = 0;
  for (int i = 0; i < raw.nTasks; i++) {
    all[n].idx = i;
    all[n].tier = tierOf(raw.tasks[i], today, all[n].dateKey);
    n++;
  }
  sortRanked(all, n, raw);
  int quota[4] = {q.overdue, q.due, q.deadline, q.undated};
  int count[4] = {0, 0, 0, 0};
  int slots = q.slots < MAX_TASKS ? q.slots : MAX_TASKS;
  static Ranked picked[MAX_RAW_TASKS];
  static bool used[MAX_RAW_TASKS];
  memset(used, 0, sizeof(used));
  int np = 0;
  for (int i = 0; i < n && np < slots; i++) {
    int b = bucketOf(all[i].tier);
    if (count[b] < quota[b]) { picked[np++] = all[i]; count[b]++; used[i] = true; }
  }
  for (int i = 0; i < n && np < slots; i++)
    if (!used[i]) picked[np++] = all[i];
  sortRanked(picked, np, raw);
  m.nTasks = 0;
  for (int i = 0; i < np; i++) {
    const RawTask &t = raw.tasks[picked[i].idx];
    Task &k = m.tasks[m.nTasks++];
    copyStr(k.title, sizeof(k.title), t.title);
    k.due[0] = 0;
    if (t.dueDay >= 0) dayMonth(t.dueDay, k.due, sizeof(k.due));
    else if (t.deadlineDay >= 0) dayMonth(t.deadlineDay, k.due, sizeof(k.due));
    else if (t.src == TASK_SRC_TODOIST && t.prio == 1) copyStr(k.due, sizeof(k.due), "P1");
    else if (t.src == TASK_SRC_TODOIST && t.prio == 2) copyStr(k.due, sizeof(k.due), "P2");
    else if (t.src == TASK_SRC_MICROSOFT && t.prio == 1) copyStr(k.due, sizeof(k.due), "wichtig");
  }
}

// ---------- Termine und Wetter je Tag ----------
void buildModel(const RawData &raw, time_t now, const TaskQuotas &q, Model &m) {
  modelClear(m);
  int32_t today = tu::localDay(now);
  for (int i = 0; i < 3; i++) {
    Day &d = m.days[i];
    int32_t day = today + i;
    int y, mo, dd;
    tu::civilFromDays(day, y, mo, dd);
    int wd = (int)((day % 7 + 7 + 4) % 7);  // 1970-01-01 war Donnerstag
    copyStr(d.name, sizeof(d.name), WD[wd]);
    snprintf(d.date, sizeof(d.date), "%d.%d.", dd, mo);

    time_t dayStart = tu::localMidnight(day);
    time_t dayEnd = tu::localMidnight(day + 1);
    for (int e = 0; e < raw.nEvents; e++) {
      const RawEvent &ev = raw.events[e];
      if (ev.allDay) {
        if (day >= ev.startDay && day < ev.endDay && d.nAllDay < MAX_ALLDAY) {
          Event &o = d.allDay[d.nAllDay++];
          copyStr(o.title, sizeof(o.title), ev.title);
          o.time[0] = 0;
          o.sortKey = 0;
        }
        continue;
      }
      // Termine mit Uhrzeit am Starttag zeigen (wie bisher), beendete weglassen
      if (ev.end > 0 && ev.end < now) continue;
      if (ev.start < dayStart || ev.start >= dayEnd) continue;
      if (d.nTimed >= MAX_TIMED) continue;
      Event &o = d.timed[d.nTimed];
      copyStr(o.title, sizeof(o.title), ev.title);
      char a[8], b[8];
      hm((time_t)ev.start, a, sizeof(a));
      if (ev.end > ev.start) {
        hm((time_t)ev.end, b, sizeof(b));
        snprintf(o.time, sizeof(o.time), "%s \xE2\x80\x93 %s", a, b);
      } else {
        copyStr(o.time, sizeof(o.time), a);
      }
      o.sortKey = (int32_t)(ev.start - dayStart) / 60;
      d.nTimed++;
    }
    // nach Beginn sortieren
    for (int a = 1; a < d.nTimed; a++) {
      Event x = d.timed[a];
      int b = a - 1;
      while (b >= 0 && d.timed[b].sortKey > x.sortKey) { d.timed[b + 1] = d.timed[b]; b--; }
      d.timed[b + 1] = x;
    }

    // Wetter: Wert um 13 Uhr lokal, heute nach 13 Uhr die aktuelle Stunde
    if (raw.weatherHours) {
      time_t target = dayStart + 13 * 3600;
      if (i == 0 && now > target) target = now;
      int64_t h = (target - raw.weatherStart) / 3600;
      if (h >= 0 && h < raw.weatherHours) {
        d.hasWeather = true;
        d.temp = raw.temp[h];
        d.icon = weatherIcon(raw.code[h]);
        copyStr(d.weatherText, sizeof(d.weatherText), weatherText(raw.code[h]));
      }
    }
  }
  pickTasks(raw, today, q, m);
  if (raw.fetchedAt > 0) hm((time_t)raw.fetchedAt, m.stand, sizeof(m.stand));
}

uint32_t modelHash(const Model &m) {
  const uint8_t *p = (const uint8_t *)&m;
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < sizeof(Model); i++) { h ^= p[i]; h *= 16777619u; }
  return h;
}
