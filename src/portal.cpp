#include "portal.h"

#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_mac.h>

#include "config.h"
#include "net.h"
#include "portal_page.h"
#include "microsoft.h"
#include "sources.h"

namespace portal {

static WebServer *server = nullptr;
static uint32_t lastActivity = 0, lastSave = 0;
static bool saved = false, finished = false;

// Schluessel, die das Webinterface setzen darf
static const char *ALLOWED[] = {"g_user", "g_cals", "g_tasks", "td_token", "td_filter", "tz_home", "loc_mode",
                                "loc_name", "loc_lat", "loc_lon", "slots", "q_over", "q_due", "q_dead", "q_undat",
                                "day_min", "night_min", "wake_h", "sleep_h", "safe_bot", "show_home", "ms_client",
                                "ms_tenant", "ms_cals", "ms_lists", "ms_planner"};
// Schluessel, die das Webinterface lesen darf (keine Geheimnisse)
static const char *READABLE[] = {"g_user", "g_email", "g_cals", "g_tasks", "td_filter", "tz_home", "loc_mode",
                                 "loc_name", "loc_lat", "loc_lon", "slots", "q_over", "q_due", "q_dead",
                                 "q_undat", "day_min", "night_min", "wake_h", "sleep_h", "safe_bot", "show_home",
                                 "ms_client", "ms_tenant", "ms_user", "ms_cals", "ms_lists", "ms_planner"};

String apSsid() {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char buf[16];
  snprintf(buf, sizeof(buf), "TRMNL-%02X%02X", mac[4], mac[5]);
  return buf;
}

String apPassword() {
  String p = cfg::get("ap_pass");
  if (p.length() >= 8) return p;
  const char *alpha = "abcdefghjkmnpqrstuvwxyz23456789";
  p = "";
  for (int i = 0; i < 10; i++) p += alpha[esp_random() % strlen(alpha)];
  cfg::set("ap_pass", p);
  return p;
}

static void touch() { lastActivity = millis(); }

// Nur Anfragen ueber das eigene Geraete-WLAN zulassen (nicht aus dem Heimnetz),
// schreibende Anfragen zusaetzlich nur mit eigenem Header (Schutz vor fremden Webseiten).
static bool allowed(bool write) {
  if (server->client().localIP() != IPAddress(192, 168, 4, 1)) {
    server->send(403, "text/plain", "nur ueber das Geraete-WLAN");
    return false;
  }
  if (write && server->header("X-TRMNL") != "1") {
    server->send(403, "text/plain", "Header fehlt");
    return false;
  }
  return true;
}

static void sendJson(int code, const JsonDocument &doc) {
  String s;
  serializeJson(doc, s);
  server->send(code, "application/json", s);
}

static void sendError(int code, const char *msg) {
  JsonDocument d;
  d["error"] = msg;
  sendJson(code, d);
}

static void handleState() {
  if (!allowed(false)) return;
  touch();
  JsonDocument d;
  d["version"] = EIGEN_FIRMWARE_VERSION;
  d["sta"] = WiFi.status() == WL_CONNECTED ? WiFi.SSID() : "";
  d["last"] = cfg::get("last_ok");
  d["err"] = cfg::get("last_err");
  d["ota"] = cfg::get("ota_last");
  JsonObject c = d["cfg"].to<JsonObject>();
  for (const char *k : READABLE) c[k] = cfg::get(k);
  JsonObject s = d["secrets"].to<JsonObject>();
  s["g_key"] = cfg::has("g_key");
  s["td_token"] = cfg::has("td_token");
  s["ms_refresh"] = cfg::has("ms_refresh");
  JsonArray w = d["wifis"].to<JsonArray>();
  cfg::Wifi list[cfg::MAX_WIFI];
  int n = cfg::loadWifis(list);
  for (int i = 0; i < n; i++) w.add(list[i].ssid);
  sendJson(200, d);
}

static bool validValue(const char *k, const String &v) {
  struct R { const char *k; int lo, hi; } ranges[] = {{"slots", 1, 12}, {"q_over", 0, 12}, {"q_due", 0, 12},
      {"q_dead", 0, 12}, {"q_undat", 0, 12}, {"day_min", 1, 1440}, {"night_min", 5, 1440}, {"wake_h", 0, 23},
      {"sleep_h", 0, 24}, {"safe_bot", 380, 480}};
  for (auto &r : ranges) {
    if (strcmp(r.k, k)) continue;
    if (!v.length()) return true;
    for (size_t i = 0; i < v.length(); i++)
      if (!isdigit((unsigned char)v[i])) return false;
    long n = v.toInt();
    return n >= r.lo && n <= r.hi;
  }
  return v.length() < 3000;
}

static void handleSet() {
  if (!allowed(true)) return;
  touch();
  JsonDocument in;
  if (deserializeJson(in, server->arg("plain"))) return sendError(400, "JSON fehlerhaft");
  for (JsonPair kv : in.as<JsonObject>()) {
    bool ok = false;
    for (const char *k : ALLOWED)
      if (!strcmp(k, kv.key().c_str())) ok = true;
    if (!ok) continue;
    String v = kv.value().as<String>();
    v.trim();
    if (!validValue(kv.key().c_str(), v)) continue;
    if (v.length()) cfg::set(kv.key().c_str(), v);
    else cfg::remove(kv.key().c_str());
  }
  saved = true;
  lastSave = millis();
  JsonDocument d;
  d["ok"] = true;
  sendJson(200, d);
}

static void handleWifi() {
  if (!allowed(true)) return;
  touch();
  JsonDocument in;
  if (deserializeJson(in, server->arg("plain"))) return sendError(400, "JSON fehlerhaft");
  String ssid = in["ssid"] | "";
  if (!ssid.length()) return sendError(400, "Name fehlt");
  cfg::saveWifi(ssid, in["pass"] | "");
  saved = true;
  lastSave = millis();
  // gleich verbinden, damit Kalender geladen werden koennen
  WiFi.begin(ssid.c_str(), (const char *)(in["pass"] | ""));
  JsonDocument d;
  d["ok"] = true;
  sendJson(200, d);
}

static void handleForget() {
  if (!allowed(true)) return;
  touch();
  JsonDocument in;
  deserializeJson(in, server->arg("plain"));
  cfg::forgetWifi(in["ssid"] | "");
  saved = true;
  lastSave = millis();
  JsonDocument d;
  d["ok"] = true;
  sendJson(200, d);
}

static void handleScan() {
  if (!allowed(false)) return;
  touch();
  int n = WiFi.scanNetworks(false, false, false, 120);
  JsonDocument d;
  JsonArray a = d.to<JsonArray>();
  for (int i = 0; i < n; i++) {
    String s = WiFi.SSID(i);
    if (!s.length()) continue;
    bool dup = false;
    for (JsonVariant v : a)
      if (v.as<String>() == s) dup = true;
    if (!dup) a.add(s);
  }
  WiFi.scanDelete();
  sendJson(200, d);
}

static void handleGoogleKey() {
  if (!allowed(true)) return;
  touch();
  JsonDocument in;
  if (deserializeJson(in, server->arg("plain"))) return sendError(400, "keine JSON-Datei");
  String email = in["client_email"] | "", key = in["private_key"] | "";
  if (!email.length() || key.indexOf("PRIVATE KEY") < 0) return sendError(400, "kein Dienstkonto-Schluessel");
  cfg::set("g_email", email);
  cfg::set("g_key", key);
  saved = true;
  lastSave = millis();
  JsonDocument d;
  d["ok"] = true;
  sendJson(200, d);
}

static void handleCals() {
  if (!allowed(false)) return;
  touch();
  if (WiFi.status() != WL_CONNECTED) return sendError(503, "Geraet ist mit keinem WLAN verbunden");
  if (time(nullptr) < 1700000000) net::syncTime();
  String out, err;
  if (!src::googleCalendarList(out, err)) return sendError(502, err.c_str());
  server->send(200, "application/json", out);
}

static void handleMsCals() {
  if (!allowed(false)) return;
  touch();
  if (WiFi.status() != WL_CONNECTED) return sendError(503, "Geraet ist mit keinem WLAN verbunden");
  if (time(nullptr) < 1700000000) net::syncTime();
  String out, err;
  if (!ms::calendarList(out, err)) return sendError(502, err.c_str());
  server->send(200, "application/json", out);
}

static void handleMsLists() {
  if (!allowed(false)) return;
  touch();
  if (WiFi.status() != WL_CONNECTED) return sendError(503, "Geraet ist mit keinem WLAN verbunden");
  if (time(nullptr) < 1700000000) net::syncTime();
  String out, err;
  if (!ms::todoLists(out, err)) return sendError(502, err.c_str());
  server->send(200, "application/json", out);
}

static void handleMsLogout() {
  if (!allowed(true)) return;
  touch();
  cfg::remove("ms_refresh");
  cfg::remove("ms_user");
  saved = true;
  lastSave = millis();
  JsonDocument d;
  d["ok"] = true;
  sendJson(200, d);
}

static void handleOta() {
  if (!allowed(true)) return;
  touch();
  cfg::setInt("ota_now", 1);  // beim naechsten Abruf sofort suchen
  saved = true;
  lastSave = millis();
  JsonDocument d;
  d["ok"] = true;
  sendJson(200, d);
}

static void handleDone() {
  if (!allowed(true)) return;
  touch();
  finished = true;
  JsonDocument d;
  d["ok"] = true;
  sendJson(200, d);
}

static void redirect() {
  touch();
  server->sendHeader("Location", "http://192.168.4.1/", true);
  server->send(302, "text/plain", "");
}

Result run(uint32_t firstMs, uint32_t afterSaveMs, bool keepSta) {
  saved = false;
  finished = false;
  WiFi.mode(keepSta ? WIFI_AP_STA : WIFI_AP);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  WiFi.softAP(apSsid().c_str(), apPassword().c_str());
  DNSServer dns;
  dns.start(53, "*", IPAddress(192, 168, 4, 1));
  WebServer web(80);
  server = &web;
  const char *hdrs[] = {"X-TRMNL"};
  web.collectHeaders(hdrs, 1);
  web.on("/", HTTP_GET, [] {
    if (!allowed(false)) return;
    touch();
    server->send(200, "text/html; charset=utf-8", PORTAL_HTML);
  });
  web.on("/api/state", HTTP_GET, handleState);
  web.on("/api/set", HTTP_POST, handleSet);
  web.on("/api/wifi", HTTP_POST, handleWifi);
  web.on("/api/wifi/forget", HTTP_POST, handleForget);
  web.on("/api/scan", HTTP_GET, handleScan);
  web.on("/api/google-key", HTTP_POST, handleGoogleKey);
  web.on("/api/cals", HTTP_GET, handleCals);
  web.on("/api/mscals", HTTP_GET, handleMsCals);
  web.on("/api/mslists", HTTP_GET, handleMsLists);
  web.on("/api/mslogout", HTTP_POST, handleMsLogout);
  web.on("/api/ota", HTTP_POST, handleOta);
  web.on("/api/done", HTTP_POST, handleDone);
  web.onNotFound(redirect);
  web.begin();
  uint32_t start = millis();
  lastActivity = 0;
  while (!finished) {
    dns.processNextRequest();
    web.handleClient();
    delay(2);
    uint32_t now = millis();
    bool clients = WiFi.softAPgetStationNum() > 0;
    if (saved) {
      // nach dem letzten Speichern noch afterSaveMs offen, solange keine Anfrage kommt
      uint32_t ref = lastActivity > lastSave ? lastActivity : lastSave;
      if (now - ref > afterSaveMs) break;
    } else if (!clients && !lastActivity && now - start > firstMs) {
      break;  // niemand hat sich verbunden
    } else if (lastActivity && now - lastActivity > (clients ? 15UL * 60000 : firstMs)) {
      break;
    }
  }
  delay(300);
  web.stop();
  dns.stop();
  server = nullptr;
  WiFi.softAPdisconnect(true);
  return {saved};
}

}  // namespace portal
