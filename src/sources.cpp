#include "sources.h"

#include <WiFi.h>
#include <mbedtls/base64.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/md.h>
#include <mbedtls/pk.h>

#include "config.h"
#include "net.h"
#include "timeutil.h"

namespace src {

// ---------- Google: Dienstkonto -> Zugriffstoken ----------
// Token im RTC-Speicher, ueberlebt den Tiefschlaf
RTC_DATA_ATTR static char gToken[1200];
RTC_DATA_ATTR static int64_t gTokenExp = 0;

static String b64url(const uint8_t *data, size_t len) {
  size_t outLen = 0;
  mbedtls_base64_encode(nullptr, 0, &outLen, data, len);
  String out;
  char *buf = (char *)malloc(outLen + 1);
  if (!buf) return out;
  mbedtls_base64_encode((unsigned char *)buf, outLen + 1, &outLen, data, len);
  buf[outLen] = 0;
  for (size_t i = 0; i < outLen; i++) {
    if (buf[i] == '+') buf[i] = '-';
    else if (buf[i] == '/') buf[i] = '_';
  }
  while (outLen && buf[outLen - 1] == '=') buf[--outLen] = 0;
  out = buf;
  free(buf);
  return out;
}

static String b64url(const String &s) { return b64url((const uint8_t *)s.c_str(), s.length()); }

static bool signRs256(const String &input, const String &pem, String &sigOut, String &err) {
  mbedtls_pk_context pk;
  mbedtls_entropy_context ent;
  mbedtls_ctr_drbg_context drbg;
  mbedtls_pk_init(&pk);
  mbedtls_entropy_init(&ent);
  mbedtls_ctr_drbg_init(&drbg);
  bool ok = false;
  uint8_t hash[32];
  static uint8_t sig[512];
  size_t sigLen = 0;
  do {
    if (mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &ent, (const uint8_t *)"trmnl", 5)) { err = "Zufall"; break; }
    if (mbedtls_pk_parse_key(&pk, (const uint8_t *)pem.c_str(), pem.length() + 1, nullptr, 0)) { err = "Schl\xC3\xBC" "ssel ung\xC3\xBCltig"; break; }
    mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), (const uint8_t *)input.c_str(), input.length(), hash);
    if (mbedtls_pk_sign(&pk, MBEDTLS_MD_SHA256, hash, 32, sig, &sigLen, mbedtls_ctr_drbg_random, &drbg)) { err = "Signatur"; break; }
    sigOut = b64url(sig, sigLen);
    ok = true;
  } while (false);
  mbedtls_pk_free(&pk);
  mbedtls_ctr_drbg_free(&drbg);
  mbedtls_entropy_free(&ent);
  return ok;
}

static const char *googleToken(String &err) {
  time_t now = time(nullptr);
  if (gTokenExp > now + 120 && gToken[0]) return gToken;
  String email = cfg::get("g_email"), key = cfg::get("g_key"), user = cfg::get("g_user");
  if (!email.length() || !key.length() || !user.length()) { err = "Google nicht eingerichtet"; return nullptr; }
  key.replace("\\n", "\n");
  String header = b64url("{\"alg\":\"RS256\",\"typ\":\"JWT\"}");
  String claims = "{\"iss\":\"" + email + "\",\"sub\":\"" + user +
                  "\",\"scope\":\"https://www.googleapis.com/auth/calendar.readonly "
                  "https://www.googleapis.com/auth/tasks.readonly\","
                  "\"aud\":\"https://oauth2.googleapis.com/token\",\"iat\":" +
                  String((long)now) + ",\"exp\":" + String((long)now + 3600) + "}";
  String unsignedJwt = header + "." + b64url(claims);
  String sig;
  if (!signRs256(unsignedJwt, key, sig, err)) return nullptr;
  String body = "grant_type=urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Ajwt-bearer&assertion=" + unsignedJwt + "." + sig;
  JsonDocument doc;
  int code = net::postJson("https://oauth2.googleapis.com/token", body, "application/x-www-form-urlencoded", doc);
  if (code != 200) {
    err = "Google-Anmeldung " + String(code) + " " + String((const char *)(doc["error"] | "")) + " " +
          String((const char *)(doc["error_description"] | ""));
    return nullptr;
  }
  const char *tok = doc["access_token"] | "";
  if (strlen(tok) >= sizeof(gToken)) { err = "Token zu lang"; return nullptr; }
  strcpy(gToken, tok);
  gTokenExp = now + (int)(doc["expires_in"] | 3600);
  return gToken;
}

static void copyTitle(char *dst, size_t n, const char *s) {
  // Zeilenumbrueche und Tabs zu Leerzeichen
  size_t i = 0;
  for (; *s && i + 1 < n; s++) dst[i++] = (*s == '\n' || *s == '\r' || *s == '\t') ? ' ' : *s;
  dst[i] = 0;
  // UTF-8 nicht mitten im Zeichen abschneiden
  while (i > 0 && ((uint8_t)dst[i - 1] & 0xC0) == 0x80) i--;
  if (i > 0 && ((uint8_t)dst[i - 1] & 0xC0) == 0xC0) dst[i - 1] = 0;
}

bool googleCalendar(RawData &raw, time_t from, time_t to, String &err) {
  String cals = cfg::get("g_cals");
  if (!cals.length()) { err = "keine Kalender"; return false; }
  const char *tok = googleToken(err);
  if (!tok) return false;
  char tmin[24], tmax[24];
  tu::formatUtc(from, tmin, sizeof(tmin));
  tu::formatUtc(to, tmax, sizeof(tmax));
  JsonDocument filter;
  JsonObject it = filter["items"].add<JsonObject>();
  it["summary"] = true;
  it["status"] = true;
  it["eventType"] = true;
  it["start"]["date"] = true;
  it["start"]["dateTime"] = true;
  it["end"]["date"] = true;
  it["end"]["dateTime"] = true;
  filter["error"]["message"] = true;
  raw.nEvents = 0;
  bool allOk = true;
  int start = 0;
  while (start < (int)cals.length()) {
    int nl = cals.indexOf('\n', start);
    if (nl < 0) nl = cals.length();
    String id = cals.substring(start, nl);
    id.trim();
    start = nl + 1;
    if (!id.length()) continue;
    String url = "https://www.googleapis.com/calendar/v3/calendars/" + net::urlEncode(id) +
                 "/events?singleEvents=true&orderBy=startTime&maxResults=60&timeMin=" + tmin + "&timeMax=" + tmax +
                 "&fields=items(summary,status,eventType,start,end)";
    JsonDocument doc;
    int code = net::getJson(url, doc, &filter, tok);
    if (code != 200) {
      allOk = false;
      err = "Kalender " + String(code) + " " + String((const char *)(doc["error"]["message"] | ""));
      continue;
    }
    for (JsonObject e : doc["items"].as<JsonArray>()) {
      if (raw.nEvents >= MAX_RAW_EVENTS) break;
      const char *status = e["status"] | "";
      const char *type = e["eventType"] | "";
      if (!strcmp(status, "cancelled") || !strcmp(type, "workingLocation")) continue;
      RawEvent &r = raw.events[raw.nEvents];
      copyTitle(r.title, sizeof(r.title), e["summary"] | "(ohne Titel)");
      if (!e["start"]["date"].isNull()) {
        r.allDay = true;
        r.startDay = tu::parseDate(e["start"]["date"] | "");
        r.endDay = tu::parseDate(e["end"]["date"] | "");
        if (r.endDay <= r.startDay) r.endDay = r.startDay + 1;
        r.start = r.end = 0;
      } else {
        r.allDay = false;
        r.start = tu::parseRfc3339(e["start"]["dateTime"] | "");
        r.end = tu::parseRfc3339(e["end"]["dateTime"] | "");
        if (!r.start) continue;
      }
      raw.nEvents++;
    }
  }
  return allOk;
}

bool googleCalendarList(String &jsonOut, String &err) {
  const char *tok = googleToken(err);
  if (!tok) return false;
  JsonDocument filter;
  JsonObject it = filter["items"].add<JsonObject>();
  it["id"] = true;
  it["summary"] = true;
  it["summaryOverride"] = true;
  it["primary"] = true;
  JsonDocument doc;
  int code = net::getJson("https://www.googleapis.com/calendar/v3/users/me/calendarList?maxResults=100", doc, &filter, tok);
  if (code != 200) { err = "Kalenderliste " + String(code); return false; }
  JsonDocument out;
  JsonArray arr = out.to<JsonArray>();
  for (JsonObject c : doc["items"].as<JsonArray>()) {
    JsonObject o = arr.add<JsonObject>();
    o["id"] = c["id"];
    o["name"] = c["summaryOverride"].isNull() ? c["summary"] : c["summaryOverride"];
    o["primary"] = c["primary"] | false;
  }
  serializeJson(out, jsonOut);
  return true;
}

bool googleTasks(RawData &raw, String &err) {
  String lists = cfg::get("g_tasks");
  if (!lists.length()) return false;
  const char *tok = googleToken(err);
  if (!tok) return false;
  JsonDocument filter;
  JsonObject it = filter["items"].add<JsonObject>();
  it["title"] = true;
  it["due"] = true;
  it["position"] = true;
  it["id"] = true;
  int start = 0, order = 0;
  bool ok = true;
  while (start < (int)lists.length()) {
    int nl = lists.indexOf('\n', start);
    if (nl < 0) nl = lists.length();
    String id = lists.substring(start, nl);
    id.trim();
    start = nl + 1;
    if (!id.length()) continue;
    JsonDocument doc;
    int code = net::getJson("https://tasks.googleapis.com/tasks/v1/lists/" + net::urlEncode(id) +
                                "/tasks?showCompleted=false&maxResults=100&fields=items(id,title,due,position)",
                            doc, &filter, tok);
    if (code != 200) { ok = false; err = "Google Tasks " + String(code); continue; }
    for (JsonObject t : doc["items"].as<JsonArray>()) {
      if (raw.nTasks >= MAX_RAW_TASKS) break;
      const char *title = t["title"] | "";
      if (!*title) continue;
      RawTask &r = raw.tasks[raw.nTasks++];
      copyTitle(r.title, sizeof(r.title), title);
      copyTitle(r.id, sizeof(r.id), t["id"] | "");
      r.dueDay = t["due"].isNull() ? -1 : tu::parseDate(t["due"] | "");
      r.deadlineDay = -1;
      r.prio = 3;
      r.addedDay = -1;
      r.order = order++;
      r.src = TASK_SRC_GOOGLE;
    }
  }
  return ok;
}

// ---------- Todoist ----------
bool todoist(RawData &raw, String &err) {
  String token = cfg::get("td_token");
  if (!token.length()) return false;
  String query = cfg::get("td_filter", "(7 days | overdue) | (deadline before: +14 days) | (no date & p1) | (no date & p2)");
  JsonDocument filter;
  JsonObject it = filter["results"].add<JsonObject>();
  for (const char *k : {"id", "content", "priority", "added_at", "child_order", "checked", "is_deleted"}) it[k] = true;
  it["due"]["date"] = true;
  it["deadline"]["date"] = true;
  JsonDocument doc;
  int code = net::getJson("https://api.todoist.com/api/v1/tasks/filter?limit=100&query=" + net::urlEncode(query), doc,
                          &filter, token.c_str());
  if (code != 200) { err = "Todoist " + String(code); return false; }
  for (JsonObject t : doc["results"].as<JsonArray>()) {
    if (raw.nTasks >= MAX_RAW_TASKS) break;
    if ((t["checked"] | false) || (t["is_deleted"] | false)) continue;
    const char *id = t["id"] | "";
    bool dup = false;
    for (int i = 0; i < raw.nTasks; i++)
      if (!strcmp(raw.tasks[i].id, id)) dup = true;
    if (dup) continue;
    RawTask &r = raw.tasks[raw.nTasks++];
    copyTitle(r.title, sizeof(r.title), t["content"] | "");
    copyTitle(r.id, sizeof(r.id), id);
    r.dueDay = t["due"]["date"].isNull() ? -1 : tu::parseDate(t["due"]["date"] | "");
    r.deadlineDay = t["deadline"]["date"].isNull() ? -1 : tu::parseDate(t["deadline"]["date"] | "");
    int p = t["priority"] | 1;  // Todoist: 4 = p1 ... 1 = p4
    r.prio = p == 4 ? 1 : (p == 3 ? 2 : 3);
    r.addedDay = t["added_at"].isNull() ? -1 : tu::parseDate(t["added_at"] | "");
    r.order = t["child_order"] | 0;
    r.src = TASK_SRC_TODOIST;
  }
  return true;
}

// ---------- Wetter (Open-Meteo) ----------
bool weather(RawData &raw, double lat, double lon, String &err) {
  String url = "https://api.open-meteo.com/v1/forecast?latitude=" + String(lat, 4) + "&longitude=" + String(lon, 4) +
               "&hourly=temperature_2m,weather_code&forecast_days=4&timezone=auto&timeformat=unixtime";
  JsonDocument doc;
  int code = net::getJson(url, doc);
  if (code != 200) { err = "Wetter " + String(code); return false; }
  JsonArray t = doc["hourly"]["time"], temp = doc["hourly"]["temperature_2m"], wc = doc["hourly"]["weather_code"];
  int n = t.size();
  if (n > WEATHER_HOURS) n = WEATHER_HOURS;
  if (!n) { err = "Wetter leer"; return false; }
  raw.weatherStart = t[0].as<int64_t>();
  for (int i = 0; i < n; i++) {
    float v = temp[i] | 0.0f;
    raw.temp[i] = (int8_t)lroundf(v);
    raw.code[i] = wc[i] | 3;
  }
  raw.weatherHours = n;
  const char *tz = doc["timezone"] | "";
  if (*tz) snprintf(raw.tz, sizeof(raw.tz), "%s", tz);
  raw.tzOffset = doc["utc_offset_seconds"] | 0;
  return true;
}

// ---------- Standort ----------
bool locate(double &lat, double &lon, String &tz, String &err) {
  // 1. WLAN-Ortung ueber BeaconDB
  int n = WiFi.scanNetworks(false, true, false, 120);
  if (n >= 2) {
    JsonDocument req;
    req["considerIp"] = false;
    JsonArray aps = req["wifiAccessPoints"].to<JsonArray>();
    for (int i = 0; i < n && i < 20; i++) {
      String ssid = WiFi.SSID(i);
      if (ssid.endsWith("_nomap")) continue;  // Betreiber will nicht kartiert werden
      JsonObject a = aps.add<JsonObject>();
      a["macAddress"] = WiFi.BSSIDstr(i);
      a["signalStrength"] = WiFi.RSSI(i);
    }
    WiFi.scanDelete();
    String body;
    serializeJson(req, body);
    JsonDocument doc;
    int code = net::postJson("https://api.beacondb.net/v1/geolocate", body, "application/json", doc);
    if (code == 200 && !doc["location"]["lat"].isNull()) {
      lat = doc["location"]["lat"];
      lon = doc["location"]["lng"];
      return true;
    }
    err = "BeaconDB " + String(code);
  } else {
    WiFi.scanDelete();
  }
  // 2. Rueckfall IP-Ortung
  JsonDocument doc;
  int code = net::getJson("https://ipapi.co/json/", doc);
  if (code == 200 && !doc["latitude"].isNull()) {
    lat = doc["latitude"];
    lon = doc["longitude"];
    tz = (const char *)(doc["timezone"] | "");
    return true;
  }
  err += " IP " + String(code);
  return false;
}

bool placeName(double lat, double lon, String &name) {
  JsonDocument filter;
  filter["city"] = true;
  filter["locality"] = true;
  filter["countryName"] = true;
  JsonDocument doc;
  int code = net::getJson("https://api.bigdatacloud.net/data/reverse-geocode-client?latitude=" + String(lat, 3) +
                              "&longitude=" + String(lon, 3) + "&localityLanguage=de",
                          doc, &filter);
  if (code != 200) return false;
  const char *c = doc["city"] | "";
  if (!*c) c = doc["locality"] | "";
  name = c;
  return name.length() > 0;
}

}  // namespace src
