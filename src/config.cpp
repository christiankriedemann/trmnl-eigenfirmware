#include "config.h"

#include <LittleFS.h>
#include <Preferences.h>
#include <nvs.h>

namespace cfg {

static Preferences prefs;
static bool started = false;

static const char *SECRET_KEYS[] = {"g_key", "td_token", "ms_refresh", "kuma_url"};
// Grosse Werte liegen als Datei im LittleFS, das NVS (20 KB) ist dafuer zu knapp
static const char *FILE_KEYS[] = {"g_key", "g_cals", "g_tasks", "ms_refresh", "ms_cals", "ms_lists"};

static bool isFileKey(const char *key) {
  for (const char *k : FILE_KEYS)
    if (!strcmp(k, key)) return true;
  return false;
}

static String pathFor(const char *key) { return String("/cfg_") + key; }

void begin() {
  if (started) return;
  LittleFS.begin(true);
  // Einmalig: Namensraum "data" der alten Firmware (u. a. Protokolle) leeren, belegt sonst das NVS.
  // Die alten Einstellungen stecken vollstaendig in der Flash-Sicherung.
  prefs.begin("eigen", false);
  if (!prefs.isKey("old_clr")) {
    Preferences old;
    if (old.begin("data", false)) {
      old.clear();
      old.end();
    }
    prefs.putBool("old_clr", true);
  }
  started = true;
  // Einmalig die WLANs der Original-Firmware uebernehmen (Namensraum "wificaptive")
  if (!prefs.isKey("wifi_migr")) {
    Preferences old;
    if (old.begin("wificaptive", true)) {
      for (int i = MAX_WIFI - 1; i >= 0; i--) {
        String k = "wifi_" + String(i) + "_ssid";
        String p = "wifi_" + String(i) + "_pswd";
        String ssid = old.getString(k.c_str(), "");
        if (ssid.length()) saveWifi(ssid, old.getString(p.c_str(), ""));
      }
      old.end();
    }
    prefs.putBool("wifi_migr", true);
  }
}

String get(const char *key, const char *def) {
  begin();
  if (isFileKey(key)) {
    File f = LittleFS.open(pathFor(key), "r");
    if (!f) return def;
    String v = f.readString();
    f.close();
    return v;
  }
  return prefs.getString(key, def);
}
int getInt(const char *key, int def) {
  begin();
  if (!prefs.isKey(key)) return def;
  String s = prefs.getString(key, "");
  if (!s.length()) return def;
  return s.toInt();
}
bool set(const char *key, const String &value) {
  begin();
  if (isFileKey(key)) {
    File f = LittleFS.open(pathFor(key), "w");
    if (!f) return false;
    size_t n = f.print(value);
    f.close();
    return n == value.length() && get(key) == value;
  }
  size_t n = prefs.putString(key, value);
  return n == value.length() && prefs.getString(key, "") == value;
}
void setInt(const char *key, int value) { set(key, String(value)); }
void remove(const char *key) {
  begin();
  if (isFileKey(key)) LittleFS.remove(pathFor(key));
  else prefs.remove(key);
}
bool has(const char *key) {
  begin();
  if (isFileKey(key)) return get(key).length() > 0;
  return prefs.isKey(key) && prefs.getString(key, "").length() > 0;
}

String nvsStats() {
  nvs_stats_t st;
  if (nvs_get_stats(nullptr, &st) != ESP_OK) return "?";
  return String(st.used_entries) + " belegt, " + String(st.free_entries) + " frei";
}

bool isSecret(const char *key) {
  for (const char *s : SECRET_KEYS)
    if (strcmp(s, key) == 0) return true;
  return strncmp(key, "w", 1) == 0 && strstr(key, "_pass") != nullptr;
}

int loadWifis(Wifi *out) {
  begin();
  int n = 0;
  for (int i = 0; i < MAX_WIFI; i++) {
    String s = prefs.getString(("w" + String(i) + "_ssid").c_str(), "");
    if (!s.length()) continue;
    out[n].ssid = s;
    out[n].pass = prefs.getString(("w" + String(i) + "_pass").c_str(), "");
    n++;
  }
  return n;
}

static void writeWifis(Wifi *list, int n) {
  for (int i = 0; i < MAX_WIFI; i++) {
    String ks = "w" + String(i) + "_ssid", kp = "w" + String(i) + "_pass";
    if (i < n) {
      prefs.putString(ks.c_str(), list[i].ssid);
      prefs.putString(kp.c_str(), list[i].pass);
    } else {
      prefs.remove(ks.c_str());
      prefs.remove(kp.c_str());
    }
  }
}

// Bekanntes Netz: nur das Passwort aktualisieren, Platz bleibt. Neues Netz: unten anhaengen
// (niedrigere Prioritaet); ist die Liste voll, ersetzt es das unterste.
void saveWifi(const String &ssid, const String &pass) {
  begin();
  Wifi list[MAX_WIFI];
  int n = loadWifis(list);
  for (int i = 0; i < n; i++) {
    if (list[i].ssid == ssid) {
      list[i].pass = pass;
      writeWifis(list, n);
      return;
    }
  }
  if (n < MAX_WIFI) n++;
  list[n - 1] = {ssid, pass};
  writeWifis(list, n);
}

// Prioritaet aendern: dir < 0 = nach oben (bevorzugt), dir > 0 = nach unten
bool moveWifi(const String &ssid, int dir) {
  begin();
  Wifi list[MAX_WIFI];
  int n = loadWifis(list);
  for (int i = 0; i < n; i++) {
    if (list[i].ssid != ssid) continue;
    int j = dir < 0 ? i - 1 : i + 1;
    if (j < 0 || j >= n) return false;
    Wifi t = list[i];
    list[i] = list[j];
    list[j] = t;
    writeWifis(list, n);
    return true;
  }
  return false;
}

void forgetWifi(const String &ssid) {
  begin();
  Wifi list[MAX_WIFI];
  int n = loadWifis(list);
  Wifi rest[MAX_WIFI];
  int m = 0;
  for (int i = 0; i < n; i++)
    if (list[i].ssid != ssid) rest[m++] = list[i];
  writeWifis(rest, m);
}

}  // namespace cfg
