#include "microsoft.h"

#include "config.h"
#include "net.h"
#include "timeutil.h"

namespace ms {

// Standard: nur eigene Kalender, nur Betreff und Zeiten (Calendars.ReadBasic).
// Mit ms_shared = 1 auch Kalender, die andere freigegeben haben (Calendars.Read.Shared;
// diese Berechtigung umfasst Termindetails, daher nur auf ausdruecklichen Wunsch).
static String scopes() {
  return cfg::getInt("ms_shared", 0) ? "offline_access%20Calendars.Read.Shared%20Tasks.Read%20User.Read"
                                     : "offline_access%20Calendars.ReadBasic%20Tasks.Read%20User.Read";
}
static const char *GRAPH = "https://graph.microsoft.com/v1.0";

// Zugriffstoken: kann ueber 3 KB lang sein (z. B. mit Gruppenangaben). Im RTC-Speicher
// (ueberlebt den Tiefschlaf) nur, wenn es hineinpasst, sonst nur fuer diesen Lauf im Heap.
RTC_DATA_ATTR static char rtcToken[3072];
RTC_DATA_ATTR static int64_t tokenExp = 0;
static String heapToken;
static const char *token = rtcToken;

bool configured() { return cfg::has("ms_client"); }
bool signedIn() { return cfg::has("ms_refresh"); }

static String authBase() {
  String t = cfg::get("ms_tenant", "organizations");
  return "https://login.microsoftonline.com/" + net::urlEncode(t) + "/oauth2/v2.0/";
}

static void copyTitle(char *dst, size_t n, const char *s) {
  size_t i = 0;
  for (; *s && i + 1 < n; s++) dst[i++] = (*s == '\n' || *s == '\r' || *s == '\t') ? ' ' : *s;
  dst[i] = 0;
  while (i > 0 && ((uint8_t)dst[i - 1] & 0xC0) == 0x80) i--;
  if (i > 0 && ((uint8_t)dst[i - 1] & 0xC0) == 0xC0) dst[i - 1] = 0;
}

// Token-Antwort auswerten und speichern
static bool storeTokens(JsonDocument &doc, String &err) {
  const char *at = doc["access_token"] | "";
  const char *rt = doc["refresh_token"] | "";
  // Microsoft rotiert Refresh-Tokens: das neue immer zuerst sichern
  if (*rt && cfg::get("ms_refresh") != rt) {
    if (!cfg::set("ms_refresh", rt)) { err = "Refresh-Token nicht gespeichert"; return false; }
  }
  if (!*at) { err = "kein Zugriffstoken"; return false; }
  size_t n = strlen(at);
  if (n < sizeof(rtcToken)) {
    strcpy(rtcToken, at);
    token = rtcToken;
    tokenExp = time(nullptr) + (int)(doc["expires_in"] | 3600);
  } else {
    heapToken = at;
    token = heapToken.c_str();
    rtcToken[0] = 0;
    tokenExp = 0;  // nach dem Schlaf neu holen
  }
  Serial.println("Microsoft-Token: " + String((unsigned)n) + " Zeichen");
  return true;
}

static const char *accessToken(String &err) {
  time_t now = time(nullptr);
  if (heapToken.length()) return token;
  if (tokenExp > now + 120 && rtcToken[0]) return rtcToken;
  String rt = cfg::get("ms_refresh");
  if (!rt.length()) { err = "Microsoft nicht angemeldet"; return nullptr; }
  String body = "client_id=" + net::urlEncode(cfg::get("ms_client")) + "&grant_type=refresh_token&scope=" + scopes() +
                "&refresh_token=" + net::urlEncode(rt);
  JsonDocument filter;
  filter["access_token"] = true;
  filter["refresh_token"] = true;
  filter["expires_in"] = true;
  filter["error"] = true;
  filter["error_description"] = true;
  JsonDocument doc;
  int code = net::postJson(authBase() + "token", body, "application/x-www-form-urlencoded", doc, &filter);
  if (code != 200) {
    String e = doc["error"] | "";
    err = "Microsoft-Anmeldung " + String(code) + " " + e;
    // Refresh-Token ungueltig (abgelaufen, widerrufen, Passwort geaendert): neu anmelden
    if (e == "invalid_grant" || e == "interaction_required") cfg::remove("ms_refresh");
    rtcToken[0] = 0;
    heapToken = "";
    return nullptr;
  }
  if (!storeTokens(doc, err)) return nullptr;
  return token;
}

bool startLogin(DeviceLogin &dl, String &err) {
  String body = "client_id=" + net::urlEncode(cfg::get("ms_client")) + "&scope=" + scopes();
  JsonDocument doc;
  int code = net::postJson(authBase() + "devicecode", body, "application/x-www-form-urlencoded", doc);
  if (code != 200) {
    err = "Ger\xC3\xA4tecode " + String(code) + " " + String((const char *)(doc["error_description"] | ""));
    if (err.length() > 160) err = err.substring(0, 160);
    return false;
  }
  dl.userCode = (const char *)(doc["user_code"] | "");
  dl.deviceCode = (const char *)(doc["device_code"] | "");
  dl.verificationUri = (const char *)(doc["verification_uri"] | "https://microsoft.com/devicelogin");
  dl.interval = doc["interval"] | 5;
  dl.expiresIn = doc["expires_in"] | 900;
  return dl.userCode.length() && dl.deviceCode.length();
}

bool finishLogin(const DeviceLogin &dl, String &err, bool (*cancel)()) {
  uint32_t t0 = millis();
  int interval = dl.interval;
  String body = "client_id=" + net::urlEncode(cfg::get("ms_client")) +
                "&grant_type=urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Adevice_code&device_code=" +
                net::urlEncode(dl.deviceCode);
  while (millis() - t0 < (uint32_t)dl.expiresIn * 1000UL) {
    for (int i = 0; i < interval * 10; i++) {
      if (cancel && cancel()) { err = "abgebrochen"; return false; }
      delay(100);
    }
    JsonDocument filter;
    filter["access_token"] = true;
    filter["refresh_token"] = true;
    filter["expires_in"] = true;
    filter["error"] = true;
    filter["error_description"] = true;
    JsonDocument doc;
    int code = net::postJson(authBase() + "token", body, "application/x-www-form-urlencoded", doc, &filter);
    if (code == 200) {
      if (!storeTokens(doc, err)) return false;
      // Kontoname fuer die Anzeige im Webinterface
      JsonDocument me, mf;
      mf["userPrincipalName"] = true;
      if (net::getJson(String(GRAPH) + "/me?$select=userPrincipalName", me, &mf, token) == 200)
        cfg::set("ms_user", me["userPrincipalName"] | "");
      return true;
    }
    String e = doc["error"] | "";
    if (e == "authorization_pending") continue;
    if (e == "slow_down") { interval += 5; continue; }
    err = "Anmeldung " + String(code) + ": " + e + " " + String((const char *)(doc["error_description"] | "")).substring(0, 120);
    return false;
  }
  err = "Anmeldecode abgelaufen";
  return false;
}

// Graph-Zeit "2026-11-26T09:00:00.0000000" (UTC per Prefer-Header) -> time_t
static time_t graphUtc(const char *s) {
  char buf[24];
  snprintf(buf, sizeof(buf), "%.19sZ", s ? s : "");
  return tu::parseRfc3339(buf);
}

static int fetchCalendar(const String &url, RawData &raw, const char *tok, String &err) {
  JsonDocument filter;
  JsonObject it = filter["value"].add<JsonObject>();
  it["subject"] = true;
  it["isAllDay"] = true;
  it["isCancelled"] = true;
  it["showAs"] = true;
  it["start"]["dateTime"] = true;
  it["end"]["dateTime"] = true;
  filter["error"]["message"] = true;
  JsonDocument doc;
  int code = net::getJson(url, doc, &filter, tok, "Prefer", "outlook.timezone=\"UTC\"");
  if (code != 200) {
    err = "Outlook-Kalender " + String(code) + " " + String((const char *)(doc["error"]["message"] | ""));
    return code;
  }
  for (JsonObject e : doc["value"].as<JsonArray>()) {
    if (raw.nEvents >= MAX_RAW_EVENTS) break;
    if (e["isCancelled"] | false) continue;
    RawEvent &r = raw.events[raw.nEvents];
    copyTitle(r.title, sizeof(r.title), e["subject"] | "(ohne Titel)");
    time_t s = graphUtc(e["start"]["dateTime"] | ""), en = graphUtc(e["end"]["dateTime"] | "");
    if (!s) continue;
    if (e["isAllDay"] | false) {
      // Ganztaegige Termine stehen auf Mitternacht in der Zone des Termins; +12 h trifft
      // fuer alle Zonen zwischen UTC-12 und UTC+12 den richtigen Kalendertag
      r.allDay = true;
      r.startDay = (int32_t)((s + 43200) / 86400);
      r.endDay = (int32_t)((en + 43200) / 86400);
      if (r.endDay <= r.startDay) r.endDay = r.startDay + 1;
      r.start = r.end = 0;
    } else {
      r.allDay = false;
      r.start = s;
      r.end = en;
    }
    raw.nEvents++;
  }
  return 200;
}

bool calendar(RawData &raw, time_t from, time_t to, String &err) {
  const char *tok = accessToken(err);
  if (!tok) return false;
  char a[24], b[24];
  tu::formatUtc(from, a, sizeof(a));
  tu::formatUtc(to, b, sizeof(b));
  String q = String("/calendarView?startDateTime=") + a + "&endDateTime=" + b +
             "&$select=subject,isAllDay,isCancelled,showAs,start,end&$orderby=start/dateTime&$top=60";
  String cals = cfg::get("ms_cals");
  if (!cals.length()) return fetchCalendar(String(GRAPH) + "/me" + q, raw, tok, err) == 200;
  bool ok = true;
  int start = 0;
  while (start < (int)cals.length()) {
    int nl = cals.indexOf('\n', start);
    if (nl < 0) nl = cals.length();
    String id = cals.substring(start, nl);
    id.trim();
    start = nl + 1;
    if (!id.length()) continue;
    if (fetchCalendar(String(GRAPH) + "/me/calendars/" + net::urlEncode(id) + q, raw, tok, err) != 200) ok = false;
  }
  return ok;
}

static bool addTask(RawData &raw, const char *title, const char *id, int32_t dueDay, int8_t prio, int32_t addedDay,
                    int32_t order) {
  if (raw.nTasks >= MAX_RAW_TASKS || !title || !*title) return false;
  // doppelt (gleicher Titel und gleiche Faelligkeit) nicht zweimal zeigen
  for (int i = 0; i < raw.nTasks; i++)
    if (!strncmp(raw.tasks[i].title, title, sizeof(raw.tasks[i].title) - 1) && raw.tasks[i].dueDay == dueDay)
      return false;
  RawTask &r = raw.tasks[raw.nTasks++];
  copyTitle(r.title, sizeof(r.title), title);
  copyTitle(r.id, sizeof(r.id), id);
  r.dueDay = dueDay;
  r.deadlineDay = -1;
  r.prio = prio;
  r.addedDay = addedDay;
  r.order = order;
  r.src = TASK_SRC_MICROSOFT;
  return true;
}

bool tasks(RawData &raw, String &err) {
  const char *tok = accessToken(err);
  if (!tok) return false;
  bool ok = true;
  int order = 0;

  // Planner: mir zugewiesene, nicht erledigte Aufgaben
  if (cfg::getInt("ms_planner", 1)) {
    JsonDocument filter;
    JsonObject it = filter["value"].add<JsonObject>();
    for (const char *k : {"id", "title", "dueDateTime", "priority", "percentComplete", "createdDateTime"}) it[k] = true;
    JsonDocument doc;
    filter["error"]["message"] = true;
    int code = net::getJson(String(GRAPH) + "/me/planner/tasks", doc, &filter, tok);
    if (code != 200) {
      ok = false;
      err = "Planner " + String(code) + " " + String((const char *)(doc["error"]["message"] | ""));
    } else {
      for (JsonObject t : doc["value"].as<JsonArray>()) {
        if ((t["percentComplete"] | 0) >= 100) continue;
        int p = t["priority"] | 5;  // 0-1 dringend, 2-4 wichtig, 5-7 mittel, 8-10 niedrig
        int8_t prio = p <= 4 ? 1 : (p <= 7 ? 2 : 3);
        int32_t due = t["dueDateTime"].isNull() ? -1 : tu::localDay(tu::parseRfc3339(t["dueDateTime"] | ""));
        int32_t added = t["createdDateTime"].isNull() ? -1 : tu::parseDate(t["createdDateTime"] | "");
        addTask(raw, t["title"] | "", t["id"] | "", due, prio, added, order++);
      }
    }
  }

  // To Do: ausgewaehlte Listen, sonst alle ausser "Gekennzeichnete E-Mails"
  String lists = cfg::get("ms_lists");
  String ids[8];
  int nIds = 0;
  if (lists.length()) {
    int start = 0;
    while (start < (int)lists.length() && nIds < 8) {
      int nl = lists.indexOf('\n', start);
      if (nl < 0) nl = lists.length();
      String id = lists.substring(start, nl);
      id.trim();
      start = nl + 1;
      if (id.length()) ids[nIds++] = id;
    }
  } else {
    JsonDocument filter;
    JsonObject it = filter["value"].add<JsonObject>();
    it["id"] = true;
    it["wellknownListName"] = true;
    JsonDocument doc;
    filter["error"]["message"] = true;
    int code = net::getJson(String(GRAPH) + "/me/todo/lists", doc, &filter, tok);
    if (code != 200) {
      ok = false;
      err = "To-Do-Listen " + String(code) + " " + String((const char *)(doc["error"]["message"] | ""));
    } else {
      for (JsonObject l : doc["value"].as<JsonArray>()) {
        if (!strcmp(l["wellknownListName"] | "", "flaggedEmails")) continue;
        if (nIds < 8) ids[nIds++] = (const char *)(l["id"] | "");
      }
    }
  }
  for (int i = 0; i < nIds; i++) {
    JsonDocument filter;
    JsonObject it = filter["value"].add<JsonObject>();
    for (const char *k : {"id", "title", "importance", "createdDateTime"}) it[k] = true;
    it["dueDateTime"]["dateTime"] = true;
    filter["error"]["message"] = true;
    JsonDocument doc;
    int code = net::getJson(String(GRAPH) + "/me/todo/lists/" + net::urlEncode(ids[i]) +
                                "/tasks?$filter=status%20ne%20%27completed%27&$top=60",
                            doc, &filter, tok);
    if (code != 200) {
      ok = false;
      err = "To-Do-Aufgaben " + String(code) + " " + String((const char *)(doc["error"]["message"] | ""));
      continue;
    }
    for (JsonObject t : doc["value"].as<JsonArray>()) {
      // Faelligkeit in To Do ist ein Kalendertag (Mitternacht in der Zone des Nutzers)
      int32_t due = t["dueDateTime"]["dateTime"].isNull() ? -1 : tu::parseDate(t["dueDateTime"]["dateTime"] | "");
      int8_t prio = !strcmp(t["importance"] | "", "high") ? 1 : 3;
      int32_t added = t["createdDateTime"].isNull() ? -1 : tu::parseDate(t["createdDateTime"] | "");
      addTask(raw, t["title"] | "", t["id"] | "", due, prio, added, order++);
    }
  }
  return ok;
}

bool calendarList(String &jsonOut, String &err) {
  const char *tok = accessToken(err);
  if (!tok) return false;
  JsonDocument filter;
  JsonObject it = filter["value"].add<JsonObject>();
  it["id"] = true;
  it["name"] = true;
  it["isDefaultCalendar"] = true;
  it["owner"]["name"] = true;
  it["owner"]["address"] = true;
  JsonDocument doc;
  int code = net::getJson(String(GRAPH) + "/me/calendars?$select=id,name,isDefaultCalendar,owner&$top=50", doc, &filter, tok);
  if (code != 200) { err = "Kalenderliste " + String(code); return false; }
  JsonDocument out;
  JsonArray arr = out.to<JsonArray>();
  for (JsonObject c : doc["value"].as<JsonArray>()) {
    JsonObject o = arr.add<JsonObject>();
    o["id"] = c["id"];
    String name = c["name"] | "";
    // Freigegebene Kalender: Besitzer dazuschreiben, damit "Kalender" nicht dreimal gleich heisst
    String owner = c["owner"]["address"] | "";
    if (owner.length() && !owner.equalsIgnoreCase(cfg::get("ms_user"))) name += " (" + String((const char *)(c["owner"]["name"] | owner.c_str())) + ")";
    o["name"] = name;
    o["primary"] = c["isDefaultCalendar"] | false;
  }
  serializeJson(out, jsonOut);
  return true;
}

bool todoLists(String &jsonOut, String &err) {
  const char *tok = accessToken(err);
  if (!tok) return false;
  JsonDocument filter;
  JsonObject it = filter["value"].add<JsonObject>();
  it["id"] = true;
  it["displayName"] = true;
  it["wellknownListName"] = true;
  JsonDocument doc;
  int code = net::getJson(String(GRAPH) + "/me/todo/lists", doc, &filter, tok);
  if (code != 200) { err = "To-Do-Listen " + String(code); return false; }
  JsonDocument out;
  JsonArray arr = out.to<JsonArray>();
  for (JsonObject l : doc["value"].as<JsonArray>()) {
    JsonObject o = arr.add<JsonObject>();
    o["id"] = l["id"];
    o["name"] = l["displayName"];
    o["flagged"] = !strcmp(l["wellknownListName"] | "", "flaggedEmails");
  }
  serializeJson(out, jsonOut);
  return true;
}

}  // namespace ms
