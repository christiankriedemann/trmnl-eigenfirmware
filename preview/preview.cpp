// Vorschau am Mac: liest Beispieldaten im Format des bisherigen n8n-Knotens "Daten",
// zeichnet mit dem Geraete-Renderer und schreibt ein PBM.
// Bauen und starten: preview/run.sh
#include <ArduinoJson.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "layout.h"

static void copy(char *dst, size_t n, const char *src) {
  snprintf(dst, n, "%s", src ? src : "");
}

static uint8_t iconFromText(const std::string &d) {
  std::string s;
  for (char ch : d) s += (char)tolower((unsigned char)ch);
  if (s.find("schnee") != std::string::npos) return WI_SNOW;
  if (s.find("regen") != std::string::npos || s.find("niesel") != std::string::npos || s.find("schauer") != std::string::npos) return WI_RAIN;
  if (s.find("gewitter") != std::string::npos || s.find("sturm") != std::string::npos) return WI_STORM;
  if (s.find("klar") != std::string::npos || s.find("sonne") != std::string::npos) return WI_SUN;
  return WI_CLOUD;
}

int main(int argc, char **argv) {
  if (argc < 3) { std::cerr << "preview <daten.json> <ausgabe.pbm> [qr|msg]\n"; return 1; }
  static uint8_t buf[CANVAS_BYTES];
  Canvas c(buf);
  std::string mode = argc > 3 ? argv[3] : "";
  if (mode == "qr") {
    renderQrScreen(c, "WIFI:T:WPA;S:TRMNL-3F2A;P:kalender42;;", "Einrichtung",
                   "1. QR-Code mit dem Handy scannen und mit dem WLAN TRMNL-3F2A verbinden.\n"
                   "2. Die Einstellungsseite \xC3\xB6" "ffnet sich von selbst. Falls nicht: http://192.168.4.1\n"
                   "3. WLAN und Konten eintragen, speichern.",
                   "Ohne Verbindung schaltet sich die Einrichtung nach 10 Minuten ab.");
  } else if (mode == "login") {
    renderLoginScreen(c, "https://microsoft.com/devicelogin", "Microsoft anmelden", "K7QX-M2PL",
                      "1. QR-Code scannen oder microsoft.com/devicelogin \xC3\xB6" "ffnen.\n"
                      "2. Den Code oben eingeben.\n3. Mit dem Office-Konto anmelden und best\xC3\xA4tigen.",
                      "Der Code gilt 15 Minuten. Taste dr\xC3\xBC" "cken bricht ab.");
  } else if (mode == "msg") {
    renderMessage(c, "Microsoft-Anmeldung abgelaufen", "Bitte Taste lang dr\xC3\xBC" "cken und neu anmelden.");
  } else {
    std::ifstream in(argv[1]);
    std::stringstream ss;
    ss << in.rdbuf();
    JsonDocument doc;
    if (deserializeJson(doc, ss.str())) { std::cerr << "JSON fehlerhaft\n"; return 1; }
    static Model m;
    modelClear(m);
    for (int i = 0; i < 3; i++) {
      JsonObject d = doc["days"][i];
      Day &day = m.days[i];
      copy(day.name, sizeof(day.name), d["dayName"] | "");
      copy(day.date, sizeof(day.date), d["dayDate"] | "");
      for (JsonObject e : d["allDay"].as<JsonArray>()) {
        if (day.nAllDay >= MAX_ALLDAY) break;
        copy(day.allDay[day.nAllDay++].title, sizeof(Event::title), e["summary"] | "");
      }
      for (JsonObject e : d["timeline"].as<JsonArray>()) {
        if (day.nTimed >= MAX_TIMED) break;
        Event &ev = day.timed[day.nTimed++];
        copy(ev.title, sizeof(ev.title), e["summary"] | "");
        copy(ev.time, sizeof(ev.time), e["timeLabel"] | "");
      }
      if (!d["weather"].isNull()) {
        day.hasWeather = true;
        day.temp = d["weather"]["temp"] | 0;
        std::string desc = d["weather"]["desc"] | "";
        copy(day.weatherText, sizeof(day.weatherText), desc.c_str());
        day.icon = d["weather"]["icon"].isNull() ? iconFromText(desc) : (uint8_t)(d["weather"]["icon"] | 0);
      }
    }
    for (JsonObject t : doc["tasks"].as<JsonArray>()) {
      if (m.nTasks >= MAX_TASKS) break;
      Task &k = m.tasks[m.nTasks++];
      copy(k.title, sizeof(k.title), t["title"] | "");
      std::string due = t["due"] | "";
      size_t p = due.find_first_of("0123456789");
      if (p != std::string::npos) due = due.substr(p);
      copy(k.due, sizeof(k.due), due.c_str());
    }
    m.batteryPercent = doc["battery"]["percent"] | -1;
    m.batteryCritical = doc["battery"]["isCritical"] | false;
    copy(m.stand, sizeof(m.stand), doc["lastUpdate"] | "");
    copy(m.place, sizeof(m.place), doc["place"] | "");
    copy(m.banner, sizeof(m.banner), doc["banner"] | "");
    LayoutOptions o;
    if (getenv("SAFE_BOTTOM")) o.safeBottom = atoi(getenv("SAFE_BOTTOM"));
    renderDashboard(c, m, o);
  }
  std::ofstream out(argv[2], std::ios::binary);
  out << "P4\n" << CANVAS_W << " " << CANVAS_H << "\n";
  for (int i = 0; i < CANVAS_BYTES; i++) out.put((char)~buf[i]);  // PBM: 1 = schwarz
  return 0;
}
