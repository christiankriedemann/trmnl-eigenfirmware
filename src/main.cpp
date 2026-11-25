// TRMNL Eigenfirmware: Kalender, Aufgaben und Wetter direkt vom Geraet, ohne eigenen Server.
#include <Arduino.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <esp_sleep.h>
#include <mbedtls/base64.h>

#include <memory>

#include "config.h"
#include "data.h"
#include "display.h"
#include "layout.h"
#include "microsoft.h"
#include "net.h"
#include "ota.h"
#include "portal.h"
#include "sources.h"
#include "timeutil.h"

static const int PIN_BUTTON = 2;
static const int PIN_BATTERY = 3;
static const char *CACHE_FILE = "/raw.bin";

RTC_DATA_ATTR static uint32_t lastHash = 0;
RTC_DATA_ATTR static uint32_t bootCount = 0;

static RawData raw;    // Stand im Flash
static RawData fresh;  // neu abgerufen
static Model model;
static String lastErr;

// ---------- Hilfen ----------

static void log(const String &s) { Serial.println(s); }

static float batteryVoltage() {
  analogRead(PIN_BATTERY);
  uint32_t mv = 0;
  for (int i = 0; i < 8; i++) mv += analogReadMilliVolts(PIN_BATTERY);
  return (mv / 8) * 2 / 1000.0f;
}

// Ladezustand wie im bisherigen Workflow: (V - 3,0) / 0,012, gedeckelt auf 0 bis 100 %.
// Am USB-Kabel liegt die Spannung ueber 4,2 V, das zaehlt als 100 % (laedt).
static int batteryPercent(float v) {
  int pct = (int)lroundf((v - 3.0f) / 0.012f);
  return pct > 100 ? 100 : (pct < 0 ? 0 : pct);
}
static const int BATT_SHOW = 25;    // Symbol mit Prozent in der Fusszeile
static const int BATT_WARN = 20;    // Balken "bitte aufladen" (frueher die Warnmail, 3,24 V)
static const int BATT_EMPTY = 5;    // ganzer Bildschirm, kein WLAN mehr, stuendlich pruefen

static bool loadCache() {
  File f = LittleFS.open(CACHE_FILE, "r");
  if (!f) return false;
  bool ok = f.read((uint8_t *)&raw, sizeof(raw)) == sizeof(raw) && raw.magic == RAW_MAGIC;
  f.close();
  if (!ok) memset(&raw, 0, sizeof(raw));
  return ok;
}

static void saveCache() {
  raw.magic = RAW_MAGIC;
  File f = LittleFS.open(CACHE_FILE, "w");
  if (!f) return;
  f.write((const uint8_t *)&raw, sizeof(raw));
  f.close();
}

// Quellen lassen sich pausieren, ohne Zugangsdaten zu loeschen (src_google, src_todoist = 0)
static bool googleOn() { return cfg::getInt("src_google", 1) != 0; }
static bool todoistOn() { return cfg::getInt("src_todoist", 1) != 0; }

static void applyZone() {
  String home = cfg::get("tz_home", "Europe/Berlin");
  const char *tz = raw.tz[0] ? raw.tz : home.c_str();
  tu::setZone(tz, raw.tzOffset);
}

static TaskQuotas quotas() {
  TaskQuotas q;
  q.slots = constrain(cfg::getInt("slots", 7), 1, 12);
  q.overdue = constrain(cfg::getInt("q_over", 4), 0, 12);
  q.due = constrain(cfg::getInt("q_due", 2), 0, 12);
  q.deadline = constrain(cfg::getInt("q_dead", 2), 0, 12);
  q.undated = constrain(cfg::getInt("q_undat", 1), 0, 12);
  return q;
}

static LayoutOptions layoutOptions() {
  LayoutOptions o;
  o.safeBottom = constrain(cfg::getInt("safe_bot", 448), 380, 480);
  return o;
}

static void fmtStamp(time_t t, char *out, int n) {
  struct tm lt;
  localtime_r(&t, &lt);
  snprintf(out, n, "%d.%d. %02d:%02d", lt.tm_mday, lt.tm_mon + 1, lt.tm_hour, lt.tm_min);
}

// ---------- Fuer das Webinterface ----------

int portalBatteryPercent() { return batteryPercent(batteryVoltage()); }

void portalRenderScreen(uint8_t *buf) {
  static Model m;
  buildModel(raw, time(nullptr), quotas(), m);
  snprintf(m.place, sizeof(m.place), "%s", raw.place);
  if (raw.fetchedAt) {
    struct tm lt;
    time_t t = (time_t)raw.fetchedAt;
    localtime_r(&t, &lt);
    snprintf(m.stand, sizeof(m.stand), "%02d:%02d", lt.tm_hour, lt.tm_min);
  }
  Canvas c(buf);
  renderDashboard(c, m, layoutOptions());
}

// ---------- Anzeige ----------

// ---------- Aktualisierungsart ----------
// Normal: Teilaktualisierung (kein Flackern). Gruendlich: nach Neustart, nach Hinweis- und
// Einrichtungsbildschirmen, beim ersten Bild des Tages nach der Nachtruhe und nach
// voll_alle Teilaktualisierungen (Standard 30).
RTC_DATA_ATTR static uint16_t partialCount = 0;
RTC_DATA_ATTR static bool fullNext = true;

static void showScreenFull() {
  display::show(true);
  partialCount = 0;
  fullNext = true;  // das Dashboard danach wieder gruendlich
}

static bool needFull() {
  if (fullNext) return true;
  if (partialCount >= constrain(cfg::getInt("voll_alle", 30), 1, 1000)) return true;
  time_t now = time(nullptr);
  if (now > 1700000000) {
    struct tm lt;
    localtime_r(&now, &lt);
    int32_t today = tu::localDay(now);
    if (cfg::getInt("voll_tag", 0) != today && lt.tm_hour >= constrain(cfg::getInt("wake_h", 6), 0, 23)) return true;
  }
  return false;
}

static void showDashboard() {
  bool full = needFull();
  bool partial = display::show(full);
  if (partial) {
    partialCount++;
  } else {
    partialCount = 0;
    fullNext = false;
    if (time(nullptr) > 1700000000) cfg::setInt("voll_tag", tu::localDay(time(nullptr)));
  }
  log(String(partial ? "Teilaktualisierung " : "gruendliche Aktualisierung ") + "(seit gruendlich: " + partialCount + ")");
}

static void showModel(bool force) {
  static Model probe;
  probe = model;
  memset(probe.stand, 0, sizeof(probe.stand));  // die Uhrzeit allein loest kein Neuzeichnen aus
  uint32_t h = modelHash(probe);
  if (!force && h == lastHash) {
    log("unveraendert, kein Neuzeichnen");
    return;
  }
  Canvas c(display::buffer());
  renderDashboard(c, model, layoutOptions());
  showDashboard();
  lastHash = h;
}

static void showSetupScreen(uint32_t minutes) {
  String ssid = portal::apSsid(), pass = portal::apPassword();
  String qr = "WIFI:T:WPA;S:" + ssid + ";P:" + pass + ";;";
  String body = "1. QR-Code mit dem Handy scannen, das verbindet mit dem WLAN " + ssid + ".\n" +
                "2. Die Einstellungsseite \xC3\xB6" "ffnet sich von selbst. Sonst im Browser: 192.168.4.1\n" +
                "3. WLAN und Konten eintragen, speichern.";
  String foot = "WLAN-Passwort: " + pass + ". Ohne Verbindung endet die Einrichtung nach " + String(minutes) +
                " Minuten.";
  Canvas c(display::buffer());
  renderQrScreen(c, qr.c_str(), "Einrichtung", body.c_str(), foot.c_str(), layoutOptions());
  showScreenFull();
  lastHash = 0;
}

// ---------- Aktualisieren ----------

static bool locationFor(double &lat, double &lon, const String &ssid) {
  if (cfg::get("loc_mode", "auto") == "fixed" && cfg::has("loc_lat")) {
    lat = cfg::get("loc_lat").toDouble();
    lon = cfg::get("loc_lon").toDouble();
    return true;
  }
  // Ortung nur bei neuem WLAN oder einmal am Tag
  time_t now = time(nullptr);
  bool cached = cfg::has("c_lat") && cfg::get("c_ssid") == ssid && now - cfg::getInt("c_at", 0) < 86400;
  if (cached) {
    lat = cfg::get("c_lat").toDouble();
    lon = cfg::get("c_lon").toDouble();
    return true;
  }
  String tz, err;
  if (src::locate(lat, lon, tz, err)) {
    cfg::set("c_lat", String(lat, 4));
    cfg::set("c_lon", String(lon, 4));
    cfg::set("c_ssid", ssid);
    cfg::setInt("c_at", (int)now);
    cfg::remove("c_place");
    log("Standort ermittelt");
    return true;
  }
  log("Ortung fehlgeschlagen: " + err);
  if (cfg::has("c_lat")) {
    lat = cfg::get("c_lat").toDouble();
    lon = cfg::get("c_lon").toDouble();
    return true;
  }
  lat = 51.16;  // Rueckfall ohne jede Ortung: Mitte Deutschlands
  lon = 10.45;
  return false;
}

static void refresh(const String &ssid) {
  if (!net::syncTime()) log("NTP fehlgeschlagen");
  time_t now = time(nullptr);
  memset(&fresh, 0, sizeof(fresh));
  String err;

  double lat, lon;
  locationFor(lat, lon, ssid);
  if (src::weather(fresh, lat, lon, err)) {
    memcpy(raw.temp, fresh.temp, sizeof(raw.temp));
    memcpy(raw.code, fresh.code, sizeof(raw.code));
    raw.weatherStart = fresh.weatherStart;
    raw.weatherHours = fresh.weatherHours;
    snprintf(raw.tz, sizeof(raw.tz), "%s", fresh.tz);
    raw.tzOffset = fresh.tzOffset;
    raw.weatherOk = true;
  } else {
    log(err);
    lastErr = err;
  }
  applyZone();

  int32_t today = tu::localDay(now);
  bool calOk = false, tasksOk = false;
  bool anyCal = (googleOn() && cfg::has("g_cals")) || (ms::configured() && ms::signedIn());
  bool calFail = false;
  fresh.nEvents = 0;
  if (googleOn() && cfg::has("g_cals")) {
    err = "";
    if (src::googleCalendar(fresh, tu::localMidnight(today), tu::localMidnight(today + 3), err)) calOk = true;
    else { log(err); lastErr = err; calFail = true; }
  }
  if (ms::configured() && ms::signedIn()) {
    err = "";
    if (ms::calendar(fresh, tu::localMidnight(today), tu::localMidnight(today + 3), err)) calOk = true;
    else { log(err); lastErr = err; calFail = true; }
  }
  if (calOk || fresh.nEvents || !anyCal) {  // Teilerfolg trotzdem uebernehmen; ohne Quelle leeren
    memcpy(raw.events, fresh.events, sizeof(raw.events));
    raw.nEvents = fresh.nEvents;
  }
  fresh.nTasks = 0;
  bool anyTaskSource = false;
  if (todoistOn() && cfg::has("td_token")) {
    anyTaskSource = true;
    err = "";
    if (src::todoist(fresh, err)) tasksOk = true; else { log(err); lastErr = err; }
  }
  if (googleOn() && cfg::has("g_tasks")) {
    anyTaskSource = true;
    err = "";
    if (src::googleTasks(fresh, err)) tasksOk = true; else { log(err); lastErr = err; }
  }
  if (ms::configured() && ms::signedIn()) {
    anyTaskSource = true;
    err = "";
    if (ms::tasks(fresh, err)) tasksOk = true; else { log(err); lastErr = err; }
  }
  if (tasksOk || !anyTaskSource) {
    memcpy(raw.tasks, fresh.tasks, sizeof(raw.tasks));
    raw.nTasks = fresh.nTasks;
  }
  bool ok = (!calFail || !anyCal) && (tasksOk || !anyTaskSource);
  if (calOk || tasksOk) raw.fetchedAt = now;

  // Ort anzeigen, wenn wir nicht in der Heimatzeitzone sind
  raw.place[0] = 0;
  String home = cfg::get("tz_home", "Europe/Berlin");
  const char *homePosix = tu::posixFor(home.c_str());
  const char *herePosix = raw.tz[0] ? tu::posixFor(raw.tz) : nullptr;
  bool away = raw.tz[0] && !(homePosix && herePosix && !strcmp(homePosix, herePosix));
  if (away) {
    int32_t off = tu::utcOffset(now);
    String name = cfg::get("c_place");
    if (!name.length() && cfg::get("loc_mode", "auto") == "fixed") name = cfg::get("loc_name");
    if (!name.length() && src::placeName(lat, lon, name)) cfg::set("c_place", name);
    char utc[16];
    int h = off / 3600, mi = abs(off / 60) % 60;
    if (mi) snprintf(utc, sizeof(utc), "UTC%+d:%02d", h, mi);
    else snprintf(utc, sizeof(utc), "UTC%+d", h);
    snprintf(raw.place, sizeof(raw.place), "%s%s%s", name.c_str(), name.length() ? ", " : "", utc);
  }
  saveCache();
  if (ok) {
    char st[24];
    fmtStamp(now, st, sizeof(st));
    cfg::set("last_ok", st);
    cfg::remove("last_err");
  } else {
    cfg::set("last_err", lastErr);
  }
}

static void buildAndShow(bool offline, bool force) {
  time_t now = time(nullptr);
  buildModel(raw, now, quotas(), model);
  snprintf(model.place, sizeof(model.place), "%s", raw.place);
  int pct = batteryPercent(batteryVoltage());
  model.batteryCritical = pct <= BATT_SHOW;
  model.batteryPercent = model.batteryCritical ? (pct / 5) * 5 : -1;
  char st[24] = "";
  if (raw.fetchedAt) fmtStamp((time_t)raw.fetchedAt, st, sizeof(st));
  if (offline) {
    snprintf(model.banner, sizeof(model.banner), "Kein WLAN \xC2\xB7 letzter Sync %s", raw.fetchedAt ? st : "noch nie");
  } else if (pct <= BATT_WARN) {
    // in 5er-Schritten, damit nicht jedes Prozent ein Neuzeichnen ausloest
    snprintf(model.banner, sizeof(model.banner), "Akku schwach (%d %%) \xC2\xB7 bitte aufladen", (pct / 5) * 5);
  } else if (ms::configured() && !ms::signedIn()) {
    snprintf(model.banner, sizeof(model.banner), "Microsoft-Anmeldung n\xC3\xB6tig \xC2\xB7 Taste kurz dr\xC3\xBC" "cken");
  } else if (raw.fetchedAt && now - raw.fetchedAt > 3600) {
    snprintf(model.banner, sizeof(model.banner), "Keine Aktualisierung seit %s \xC2\xB7 %s", st, lastErr.c_str());
  }
  int ev = 0;
  for (int i = 0; i < 3; i++) ev += model.days[i].nAllDay + model.days[i].nTimed;
  log("Anzeige: " + String(ev) + " Termine, " + String(model.nTasks) + " Aufgaben, Wetter " +
      (model.days[0].hasWeather ? String(model.days[0].temp) + " Grad" : String("fehlt")) + ", Zone " + String(raw.tz) +
      (model.banner[0] ? String(", Balken: ") + model.banner : String("")));
  showModel(force);
}

// ---------- Microsoft-Anmeldung ----------

static bool buttonPressed() { return digitalRead(PIN_BUTTON) == LOW; }

static void microsoftLogin() {
  if (!net::syncTime()) log("NTP fehlgeschlagen");
  ms::DeviceLogin dl;
  String err;
  if (!ms::startLogin(dl, err)) {
    log(err);
    cfg::set("last_err", err);
    Canvas c(display::buffer());
    renderMessage(c, "Microsoft-Anmeldung nicht m\xC3\xB6glich", err.c_str(), layoutOptions());
    showScreenFull();
    lastHash = 0;
    delay(8000);
    return;
  }
  log("Microsoft-Anmeldung: Code " + dl.userCode);
  String shown = dl.verificationUri;
  shown.replace("https://", "");
  String body = "1. QR-Code scannen oder " + shown + " \xC3\xB6" "ffnen.\n" +
                "2. Den Code oben eingeben.\n" + "3. Mit dem Office-Konto anmelden und best\xC3\xA4tigen.";
  String foot = "Der Code gilt " + String(dl.expiresIn / 60) + " Minuten. Taste dr\xC3\xBC" "cken bricht ab.";
  Canvas c(display::buffer());
  renderLoginScreen(c, dl.verificationUri.c_str(), "Microsoft anmelden", dl.userCode.c_str(), body.c_str(),
                    foot.c_str(), layoutOptions());
  showScreenFull();
  lastHash = 0;
  delay(1500);  // Taste loslassen lassen
  if (ms::finishLogin(dl, err, buttonPressed)) {
    log("Microsoft angemeldet: " + cfg::get("ms_user"));
    cfg::remove("ms_err");
  } else {
    log("Microsoft-Fehler: " + err);
    cfg::set("ms_err", err);
  }
}

// ---------- Schlafen ----------

static void sleepNow() {
  time_t now = time(nullptr);
  int dayMin = constrain(cfg::getInt("day_min", 10), 1, 1440);
  int wakeH = constrain(cfg::getInt("wake_h", 6), 0, 23), sleepH = constrain(cfg::getInt("sleep_h", 23), 1, 24);
  uint64_t secs = (uint64_t)dayMin * 60;
  if (now > 1700000000) {
    struct tm lt;
    localtime_r(&now, &lt);
    if (lt.tm_hour >= sleepH || lt.tm_hour < wakeH) {
      int32_t day = tu::localDay(now) + (lt.tm_hour >= sleepH ? 1 : 0);
      time_t wake = tu::localMidnight(day) + wakeH * 3600;
      secs = wake > now ? wake - now : 60;
      if (secs > 3600) secs = 3600;  // nachts stuendlich, wie bisher
    } else {
      secs = secs - (now % secs) + 5;  // auf volle Takt-Minuten ausrichten
    }
  }
  log("Schlafe " + String((unsigned long)secs) + " s");
  display::sleep();
  net::off();
  Serial.flush();
  pinMode(PIN_BUTTON, INPUT);
  esp_deep_sleep_enable_gpio_wakeup(1ULL << PIN_BUTTON, ESP_GPIO_WAKEUP_GPIO_LOW);
  esp_sleep_enable_timer_wakeup(secs * 1000000ULL);
  esp_deep_sleep_start();
}

// ---------- Befehle ueber USB ----------
// set <schluessel> <wert> | setb64 <schluessel> <base64> | del <schluessel> | get <schluessel>
// wifi <ssid><TAB><passwort> | status | run | portal | reboot
// Geheimnisse werden nie ausgegeben.
static bool wantMsLogin = false;
static bool wantOta = false;

static bool serialSession() {
  uint32_t until = millis() + 5000;
  uint32_t nextHello = 0;
  bool talked = false;
  String line;
  while (millis() < until) {
    // Meldung wiederholen: nach einem Neustart meldet sich USB erst nach einem Moment wieder an
    if (!talked && millis() >= nextHello) {
      Serial.println("TRMNL-Eigenfirmware " EIGEN_FIRMWARE_VERSION " bereit");
      nextHello = millis() + 400;
    }
    while (Serial.available()) {
      char ch = Serial.read();
      if (ch == '\r') continue;
      if (ch != '\n') { line += ch; continue; }
      until = millis() + 60000;
      talked = true;
      line.trim();
      int sp = line.indexOf(' ');
      String cmd = sp < 0 ? line : line.substring(0, sp);
      String rest = sp < 0 ? "" : line.substring(sp + 1);
      line = "";
      if (cmd == "set" || cmd == "setb64") {
        int sp2 = rest.indexOf(' ');
        String key = sp2 < 0 ? rest : rest.substring(0, sp2);
        String val = sp2 < 0 ? "" : rest.substring(sp2 + 1);
        if (cmd == "setb64") {
          size_t outLen = 0;
          std::unique_ptr<uint8_t[]> buf(new uint8_t[val.length() + 1]);
          mbedtls_base64_decode(buf.get(), val.length(), &outLen, (const uint8_t *)val.c_str(), val.length());
          val = String((const char *)buf.get(), outLen);
        }
        if (!cfg::set(key.c_str(), val)) {
          Serial.println("FEHLER " + key + " nicht gespeichert");
          continue;
        }
        Serial.println("ok " + key + (cfg::isSecret(key.c_str()) ? String(" (geheim, ") + val.length() + " Zeichen)" : " = " + val));
      } else if (cmd == "del") {
        cfg::remove(rest.c_str());
        Serial.println("ok");
      } else if (cmd == "get") {
        Serial.println(cfg::isSecret(rest.c_str()) ? (cfg::has(rest.c_str()) ? "(gesetzt)" : "(leer)") : cfg::get(rest.c_str()));
      } else if (cmd == "wifi") {
        int tab = rest.indexOf('\t');
        cfg::saveWifi(tab < 0 ? rest : rest.substring(0, tab), tab < 0 ? "" : rest.substring(tab + 1));
        Serial.println("ok wlan");
      } else if (cmd == "status") {
        cfg::Wifi w[cfg::MAX_WIFI];
        int n = cfg::loadWifis(w);
        Serial.print("wlans:");
        for (int i = 0; i < n; i++) Serial.print(" [" + w[i].ssid + "]");
        Serial.println();
        Serial.println("google: " + cfg::get("g_email") + " fuer " + cfg::get("g_user") + ", Schluessel " +
                       (cfg::has("g_key") ? "ja" : "nein"));
        Serial.println("kalender: " + cfg::get("g_cals"));
        Serial.println(String("todoist: ") + (cfg::has("td_token") ? "ja" : "nein") + (todoistOn() ? "" : " (pausiert)"));
        if (!googleOn()) Serial.println("google: pausiert");
        Serial.println("letzte Aktualisierung: " + cfg::get("last_ok") + " | Fehler: " + cfg::get("last_err"));
        Serial.println("akku: " + String(batteryVoltage(), 2) + " V");
        Serial.println("nvs: " + cfg::nvsStats());
        Serial.println("microsoft: " + cfg::get("ms_user") + " | Fehler: " + cfg::get("ms_err"));
        Serial.println("fertig");
      } else if (cmd == "ota") {
        wantOta = true;
        Serial.println("ok ota (Suche beim naechsten Abruf)");
      } else if (cmd == "version") {
        Serial.println(String(EIGEN_FIRMWARE_VERSION) + (ota::pendingVerify() ? " (auf Probe)" : "") + " | " +
                       cfg::get("ota_last"));
      } else if (cmd == "mslogin") {
        wantMsLogin = true;
        Serial.println("ok mslogin");
        Serial.flush();
        return false;
      } else if (cmd == "run" || cmd == "portal") {
        Serial.println("ok " + cmd);
        Serial.flush();
        return cmd == "portal";
      } else if (cmd == "reboot") {
        Serial.println("ok reboot");
        Serial.flush();
        delay(100);
        ESP.restart();
      } else if (cmd.length()) {
        Serial.println("unbekannt: " + cmd);
      }
    }
    delay(10);
  }
  return false;
}

// ---------- Ablauf ----------

void setup() {
  Serial.begin(115200);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  bootCount++;
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  bool button = cause == ESP_SLEEP_WAKEUP_GPIO;
  bool timer = cause == ESP_SLEEP_WAKEUP_TIMER;
  bool coldStart = !button && !timer;

  cfg::begin();
  LittleFS.begin(true);
  loadCache();
  applyZone();

  // Akku fast leer: Hinweis zeigen, kein WLAN, stuendlich nachsehen, ob geladen wird
  {
    int pct = batteryPercent(batteryVoltage());
    if (pct <= BATT_EMPTY && !coldStart) {
      if (lastHash != 0xBA77E21) {
        Canvas c(display::buffer());
        renderMessage(c, "Bitte aufladen",
                      "Der Akku ist fast leer. Das Display aktualisiert sich erst wieder, wenn es geladen wird.\n\n"
                      "USB-C-Kabel anschlie\xC3\x9F" "en, dann geht es von selbst weiter.",
                      layoutOptions());
        showScreenFull();
        lastHash = 0xBA77E21;
      }
      log("Akku leer (" + String(pct) + " %), schlafe 1 h");
      display::sleep();
      pinMode(PIN_BUTTON, INPUT);
      esp_deep_sleep_enable_gpio_wakeup(1ULL << PIN_BUTTON, ESP_GPIO_WAKEUP_GPIO_LOW);
      esp_sleep_enable_timer_wakeup(3600ULL * 1000000ULL);
      esp_deep_sleep_start();
    }
  }

  // Befehlsfenster nur nach Neustart (dann ist meist USB dran); spart im Takt Strom
  bool wantPortal = coldStart ? serialSession() : false;

  // Langer Tastendruck (2 s) = Einrichtung, kurzer = sofort aktualisieren
  if (button) {
    uint32_t t0 = millis();
    while (digitalRead(PIN_BUTTON) == LOW && millis() - t0 < 2500) delay(20);
    if (millis() - t0 >= 2000) wantPortal = true;
  }

  cfg::Wifi w[cfg::MAX_WIFI];
  bool haveWifi = cfg::loadWifis(w) > 0;
  bool configured = cfg::has("g_cals") || cfg::has("td_token") || ms::configured();
  bool skipPortal = coldStart && cfg::getInt("no_portal", 0) == 1;  // fuer Tests ueber USB

  if ((coldStart && !skipPortal) || wantPortal || !haveWifi || !configured) {
    // Nach Neustart 3 Minuten, ohne Einrichtung 10 Minuten
    uint32_t minutes = (!haveWifi || !configured) ? 10 : 3;
    String ssid = haveWifi ? net::connect() : "";
    if (ssid.length()) net::syncTime();
    showSetupScreen(minutes);
    portal::run(minutes * 60000UL, 3 * 60000UL, true);
    if (cfg::getInt("ms_now", 0) == 1) {  // im Webinterface auf "Am Display anmelden" getippt
      cfg::remove("ms_now");
      wantMsLogin = true;
    }
  }

  String ssid = net::connect();
  if (!ssid.length()) {
    log("kein WLAN");
    if (raw.fetchedAt) {
      buildAndShow(true, false);
      ota::markHealthy();
    } else {
      Canvas c(display::buffer());
      renderMessage(c, "Kein WLAN",
                    "Taste auf der R\xC3\xBC" "ckseite 2 Sekunden halten, um ein WLAN einzurichten.", layoutOptions());
      showScreenFull();
      lastHash = 0;
    }
    sleepNow();
  }
  log("WLAN: " + ssid + ", Firmware " EIGEN_FIRMWARE_VERSION + String(ota::pendingVerify() ? " (auf Probe)" : ""));
  // Anmeldung nur nach Neustart, Tastendruck oder auf Anforderung, nicht im Takt (Strom)
  if (ms::configured() && (!ms::signedIn() || wantMsLogin) && (wantMsLogin || button || coldStart)) microsoftLogin();
  refresh(ssid);
  // Update suchen: einmal am Tag, nach Tastendruck lang/USB-Befehl sofort
  if (!ota::pendingVerify()) {
    ota::Result u = ota::check(wantOta || cfg::getInt("ota_now", 0) == 1, [](const String &v) {
      log("Update auf " + v + " wird geladen");
      Canvas c(display::buffer());
      String body = "Version " + v + " wird geladen und gepr\xC3\xBC" "ft. Das dauert etwa eine Minute.\n\n"
                    "Danach startet das Display neu und zeigt wieder Termine und Aufgaben.";
      renderMessage(c, "Update wird installiert", body.c_str(), layoutOptions());
      showScreenFull();
      lastHash = 0;
    });
    cfg::remove("ota_now");
    if (u.error.length()) log("Update: " + u.error);
    if (u.installed) {
      log("Update eingespielt, Neustart");
      display::sleep();
      delay(200);
      ESP.restart();
    }
  }
  net::off();
  buildAndShow(false, button || coldStart);
  ota::markHealthy();  // Lauf geschafft: neue Version bestaetigen
  sleepNow();
}

void loop() {}
