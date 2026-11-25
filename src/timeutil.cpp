#include "timeutil.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tzdb.h"

namespace tu {

const char *posixFor(const char *iana) {
  int lo = 0, hi = TZ_DB_COUNT - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    int c = strcmp(TZ_DB[mid].name, iana);
    if (c == 0) return TZ_DB[mid].posix;
    if (c < 0) lo = mid + 1; else hi = mid - 1;
  }
  return nullptr;
}

bool setZone(const char *iana, int fallbackOffsetSec) {
  const char *p = posixFor(iana);
  char buf[24];
  if (!p) {
    // POSIX zaehlt umgekehrt: UTC+7 ist "<+07>-7"
    int h = fallbackOffsetSec / 3600, m = abs(fallbackOffsetSec / 60) % 60;
    snprintf(buf, sizeof(buf), "UTC%s%d:%02d", h > 0 ? "-" : "+", abs(h), m);
    p = buf;
  }
  setenv("TZ", p, 1);
  tzset();
  return p != buf;
}

int32_t daysFromCivil(int y, int m, int d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int)doe - 719468;
}

void civilFromDays(int32_t z, int &y, int &m, int &d) {
  z += 719468;
  const int era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = (unsigned)(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  y = (int)yoe + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  d = doy - (153 * mp + 2) / 5 + 1;
  m = mp < 10 ? mp + 3 : mp - 9;
  y += m <= 2;
}

int32_t localDay(time_t t) {
  struct tm lt;
  localtime_r(&t, &lt);
  return daysFromCivil(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
}

time_t localMidnight(int32_t day) {
  int y, m, d;
  civilFromDays(day, y, m, d);
  struct tm lt = {};
  lt.tm_year = y - 1900;
  lt.tm_mon = m - 1;
  lt.tm_mday = d;
  lt.tm_isdst = -1;
  return mktime(&lt);
}

int32_t utcOffset(time_t t) {
  struct tm lt;
  localtime_r(&t, &lt);
  int32_t ld = daysFromCivil(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday);
  int64_t localSec = (int64_t)ld * 86400 + lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec;
  return (int32_t)(localSec - (int64_t)t);
}

static int num(const char *s, int n) {
  int v = 0;
  for (int i = 0; i < n; i++) {
    if (s[i] < '0' || s[i] > '9') return -1;
    v = v * 10 + (s[i] - '0');
  }
  return v;
}

int32_t parseDate(const char *s) {
  if (!s || strlen(s) < 10) return -1;
  int y = num(s, 4), m = num(s + 5, 2), d = num(s + 8, 2);
  if (y < 0 || m < 1 || d < 1) return -1;
  return daysFromCivil(y, m, d);
}

time_t parseRfc3339(const char *s) {
  if (!s || strlen(s) < 19) return 0;
  int32_t day = parseDate(s);
  int hh = num(s + 11, 2), mi = num(s + 14, 2), ss = num(s + 17, 2);
  if (day < 0 || hh < 0 || mi < 0 || ss < 0) return 0;
  int64_t t = (int64_t)day * 86400 + hh * 3600 + mi * 60 + ss;
  const char *p = s + 19;
  if (*p == '.') { p++; while (*p >= '0' && *p <= '9') p++; }
  if (*p == '+' || *p == '-') {
    int oh = num(p + 1, 2), om = num(p + 4, 2);
    if (oh >= 0 && om >= 0) {
      int off = oh * 3600 + om * 60;
      t -= (*p == '+') ? off : -off;
    }
  }
  return (time_t)t;
}

void formatUtc(time_t t, char *out, int n) {
  struct tm g;
  gmtime_r(&t, &g);
  strftime(out, n, "%Y-%m-%dT%H:%M:%SZ", &g);
}

}  // namespace tu
