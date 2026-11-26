#include "net.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_sntp.h>

#include "config.h"

extern const uint8_t x509_crt_bundle_start[] asm("_binary_data_x509_crt_bundle_bin_start");

namespace net {

const char *USER_AGENT = "TRMNL-Eigenfirmware/" EIGEN_FIRMWARE_VERSION;

static int lastSeen = 0;  // wie viele bekannte Netze die letzte Suche gefunden hat

static String connectOnce(uint32_t timeoutMs) {
  cfg::Wifi known[cfg::MAX_WIFI];
  int n = cfg::loadWifis(known);
  if (!n) return "";
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("trmnl-eigen");
  // Reihenfolge: das oberste gespeicherte Netz mit brauchbarem Empfang (>= -80 dBm) zuerst.
  // Hat keins genug Empfang, das staerkste bekannte. Danach die uebrigen nach Prioritaet.
  static const int GOOD_RSSI = -80;
  int rssi[cfg::MAX_WIFI];
  for (int k = 0; k < n; k++) rssi[k] = -1000;
  int found = WiFi.scanNetworks(false, false, false, 150);
  for (int i = 0; i < found; i++) {
    String s = WiFi.SSID(i);
    for (int k = 0; k < n; k++)
      if (known[k].ssid == s && WiFi.RSSI(i) > rssi[k]) rssi[k] = WiFi.RSSI(i);
  }
  WiFi.scanDelete();
  lastSeen = 0;
  for (int k = 0; k < n; k++)
    if (rssi[k] > -1000) lastSeen++;
  int order[cfg::MAX_WIFI];
  int m = 0;
  bool used[cfg::MAX_WIFI] = {false};
  for (int k = 0; k < n; k++)
    if (rssi[k] >= GOOD_RSSI) { order[m++] = k; used[k] = true; break; }
  int best = -1;
  for (int k = 0; k < n; k++)
    if (!used[k] && rssi[k] > -1000 && (best < 0 || rssi[k] > rssi[best])) best = k;
  if (best >= 0) { order[m++] = best; used[best] = true; }
  for (int k = 0; k < n; k++)
    if (!used[k] && rssi[k] > -1000) { order[m++] = k; used[k] = true; }  // gesehen, schwach
  for (int k = 0; k < n; k++)
    if (!used[k]) order[m++] = k;  // nicht gesehen (versteckt oder verschlafener Hotspot)
  for (int j = 0; j < m; j++) {
    const cfg::Wifi &w = known[order[j]];
    WiFi.begin(w.ssid.c_str(), w.pass.c_str());
    uint32_t t0 = millis();
    // Nicht gesehene Netze nur kurz probieren (spart Akku, wenn unterwegs keins da ist)
    uint32_t limit = rssi[order[j]] > -1000 ? timeoutMs : 5000;
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < limit) delay(100);
    if (WiFi.status() == WL_CONNECTED) return w.ssid;
    WiFi.disconnect(false, false);
    delay(200);
  }
  return "";
}

String connect(uint32_t timeoutMs) {
  String s = connectOnce(timeoutMs);
  if (s.length() || lastSeen == 0) return s;  // kein bekanntes Netz in Reichweite: nicht nochmal
  // zweiter Versuch nach kurzer Pause (Router, Funk, gerade verlassener Einrichtungsmodus)
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);
  delay(1500);
  return connectOnce(timeoutMs);
}

// Zeitpunkt der letzten echten Synchronisation (uebersteht den Tiefschlaf). Gesetzt im Rueckruf,
// damit auch eine Antwort zaehlt, die erst nach der Wartezeit waehrend der Abrufe eintrifft.
RTC_DATA_ATTR static int64_t lastSync = 0;
static volatile uint32_t syncMs = 0;
static uint32_t syncStart = 0;

static void onSync(struct timeval *) {
  lastSync = time(nullptr);
  syncMs = millis() - syncStart;
}

String syncInfo() {
  if (!syncStart) return "";
  return syncMs ? "NTP nach " + String(syncMs) + " ms" : "NTP ohne Antwort";
}

bool syncTime(uint32_t timeoutMs) {
  // Auf die echte Synchronisation warten, nicht nur auf "Uhr nicht mehr 1970": nach einem
  // Neustart ohne WLAN steht die Uhr auf einer Naeherung (letzte Synchronisation).
  // Die Uhr laeuft im Tiefschlaf weiter; abgleichen nach Neustart und dann alle 6 Stunden.
  time_t now = time(nullptr);
  if (lastSync > 0 && now > lastSync && now - lastSync < 6 * 3600) return true;
  syncMs = 0;
  syncStart = millis();
  sntp_set_time_sync_notification_cb(onSync);
  sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
  configTime(0, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
  while (!syncMs && millis() - syncStart < timeoutMs) delay(100);
  return syncMs != 0;
}

void off() {
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);
}

static int request(const char *method, const String &url, const String *body, const char *contentType,
                   JsonDocument &doc, const JsonDocument *filter, const char *bearer, const char *extraName = nullptr,
                   const char *extraValue = nullptr) {
  WiFiClientSecure client;
  client.setCACertBundle(x509_crt_bundle_start);
  client.setTimeout(15);
  HTTPClient http;
  http.useHTTP10(true);  // keine Chunked-Antworten, damit der Stream direkt lesbar ist
  // Umleitungen nur ohne Token folgen (z. B. GitHub-Release -> Speicher): der Arduino-Client
  // wuerde den Authorization-Header sonst an das Umleitungsziel mitschicken.
  if (!bearer) http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(15000);
  http.setUserAgent(USER_AGENT);
  if (!http.begin(client, url)) return -1;
  http.addHeader("Accept", "application/json");
  if (bearer) http.addHeader("Authorization", String("Bearer ") + bearer);
  if (contentType) http.addHeader("Content-Type", contentType);
  if (extraName && extraValue) http.addHeader(extraName, extraValue);
  int code = body ? http.sendRequest(method, *body) : http.sendRequest(method);
  if (code > 0) {
    DeserializationError err = filter ? deserializeJson(doc, http.getStream(), DeserializationOption::Filter(*filter))
                                      : deserializeJson(doc, http.getStream());
    if (err && code == 200) code = -2;
  }
  http.end();
  return code;
}

int getJson(const String &url, JsonDocument &doc, const JsonDocument *filter, const char *bearer, const char *extraName,
            const char *extraValue) {
  return request("GET", url, nullptr, nullptr, doc, filter, bearer, extraName, extraValue);
}

int postJson(const String &url, const String &body, const char *contentType, JsonDocument &doc,
             const JsonDocument *filter, const char *bearer) {
  return request("POST", url, &body, contentType, doc, filter, bearer);
}

String urlEncode(const String &s) {
  String o;
  const char *hex = "0123456789ABCDEF";
  for (size_t i = 0; i < s.length(); i++) {
    uint8_t c = (uint8_t)s[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += (char)c;
    else { o += '%'; o += hex[c >> 4]; o += hex[c & 15]; }
  }
  return o;
}

}  // namespace net
