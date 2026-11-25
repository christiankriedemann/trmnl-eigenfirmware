#pragma once
#include <Arduino.h>

// Einstellungen im NVS (Namensraum "eigen"). Geheimnisse werden nie ausgegeben.
namespace cfg {

static const int MAX_WIFI = 5;

struct Wifi {
  String ssid;
  String pass;
};

void begin();
String get(const char *key, const char *def = "");
int getInt(const char *key, int def);
bool set(const char *key, const String &value);  // false = nicht gespeichert
void setInt(const char *key, int value);
void remove(const char *key);
bool has(const char *key);
bool isSecret(const char *key);
String nvsStats();

// WLANs: eigene Liste, beim ersten Start aus der Original-Firmware uebernommen
int loadWifis(Wifi *out);
void saveWifi(const String &ssid, const String &pass);  // neu: unten anhaengen
bool moveWifi(const String &ssid, int dir);              // dir < 0 = hoeher priorisieren
void forgetWifi(const String &ssid);

// Bekannte Schluessel (alle hoechstens 15 Zeichen)
// g_email, g_key (Dienstkonto), g_user (Nutzer, fuer den gelesen wird), g_cals (Kalender-IDs, Zeilen)
// g_tasks (Google-Tasks-Listen, Zeilen; leer = keine)
// td_token, td_filter (Todoist)
// tz_home (IANA), loc_mode (auto|fixed), loc_name, loc_lat, loc_lon
// slots, q_over, q_due, q_dead, q_undat (Aufgaben-Quoten)
// day_min, night_min, wake_h, sleep_h (Takt), safe_bot (unterer Rand)
// show_home (Heimatzeit zusaetzlich zeigen 0/1)

}  // namespace cfg
