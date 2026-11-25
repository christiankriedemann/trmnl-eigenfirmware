#include "net.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "config.h"

extern const uint8_t x509_crt_bundle_start[] asm("_binary_data_x509_crt_bundle_bin_start");

namespace net {

const char *USER_AGENT = "TRMNL-Eigenfirmware/" EIGEN_FIRMWARE_VERSION;

static String connectOnce(uint32_t timeoutMs) {
  cfg::Wifi known[cfg::MAX_WIFI];
  int n = cfg::loadWifis(known);
  if (!n) return "";
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("trmnl-eigen");
  // Staerkstes bekanntes Netz waehlen; wird keins gesehen, alle der Reihe nach versuchen
  int found = WiFi.scanNetworks(false, false, false, 150);
  int order[cfg::MAX_WIFI];
  int m = 0;
  int best = -1, bestRssi = -1000;
  for (int i = 0; i < found; i++) {
    String s = WiFi.SSID(i);
    for (int k = 0; k < n; k++)
      if (known[k].ssid == s && WiFi.RSSI(i) > bestRssi) { best = k; bestRssi = WiFi.RSSI(i); }
  }
  WiFi.scanDelete();
  if (best >= 0) order[m++] = best;
  for (int k = 0; k < n; k++)
    if (k != best) order[m++] = k;
  for (int j = 0; j < m; j++) {
    const cfg::Wifi &w = known[order[j]];
    WiFi.begin(w.ssid.c_str(), w.pass.c_str());
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) delay(100);
    if (WiFi.status() == WL_CONNECTED) return w.ssid;
    WiFi.disconnect(false, false);
    delay(200);
  }
  return "";
}

String connect(uint32_t timeoutMs) {
  String s = connectOnce(timeoutMs);
  if (s.length()) return s;
  // zweiter Versuch nach kurzer Pause (Router, Funk, gerade verlassener Einrichtungsmodus)
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);
  delay(1500);
  return connectOnce(timeoutMs);
}

bool syncTime(uint32_t timeoutMs) {
  configTime(0, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
  uint32_t t0 = millis();
  while (time(nullptr) < 1700000000 && millis() - t0 < timeoutMs) delay(100);
  return time(nullptr) >= 1700000000;
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
