#include "ota.h"

#include <HTTPClient.h>
#include <Update.h>
#include <WiFiClientSecure.h>
#include <esp_ota_ops.h>
#include <mbedtls/base64.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>

#include "config.h"
#include "net.h"
#include "ota_key.h"

extern const uint8_t x509_crt_bundle_start[] asm("_binary_data_x509_crt_bundle_bin_start");

// Arduino bestaetigt eine neue Version sonst sofort beim Start. Wir bestaetigen selbst,
// erst wenn ein Lauf durchgekommen ist (markHealthy).
extern "C" bool verifyRollbackLater() { return true; }

namespace ota {

const char *DEFAULT_MANIFEST =
    "https://github.com/christiankriedemann/trmnl-eigenfirmware/releases/latest/download/manifest.json";

static int cmpVersion(const String &a, const String &b) {
  int ia = 0, ib = 0;
  for (int part = 0; part < 3; part++) {
    long va = 0, vb = 0;
    while (ia < (int)a.length() && isdigit((unsigned char)a[ia])) va = va * 10 + (a[ia++] - '0');
    while (ib < (int)b.length() && isdigit((unsigned char)b[ib])) vb = vb * 10 + (b[ib++] - '0');
    if (va != vb) return va < vb ? -1 : 1;
    if (ia < (int)a.length() && a[ia] == '.') ia++;
    if (ib < (int)b.length() && b[ib] == '.') ib++;
  }
  return 0;
}

static bool hexToBytes(const char *hex, uint8_t *out, size_t n) {
  if (strlen(hex) != n * 2) return false;
  for (size_t i = 0; i < n; i++) {
    char b[3] = {hex[2 * i], hex[2 * i + 1], 0};
    char *end;
    out[i] = (uint8_t)strtol(b, &end, 16);
    if (*end) return false;
  }
  return true;
}

// Signiert ist die Nachricht "trmnl-ota\n<version>\n<sha256 hex>": damit ist auch die
// Versionsnummer geschuetzt (keine aeltere, echt signierte Firmware als "neu" unterschiebbar).
static bool verifySignature(const String &version, const char *shaHex, const String &sigB64, String &err) {
  String msg = "trmnl-ota\n" + version + "\n" + shaHex;
  uint8_t hash[32];
  mbedtls_sha256((const uint8_t *)msg.c_str(), msg.length(), hash, 0);
  uint8_t sig[160];
  size_t sigLen = 0;
  if (mbedtls_base64_decode(sig, sizeof(sig), &sigLen, (const uint8_t *)sigB64.c_str(), sigB64.length())) {
    err = "Signatur unlesbar";
    return false;
  }
  mbedtls_pk_context pk;
  mbedtls_pk_init(&pk);
  bool ok = false;
  if (mbedtls_pk_parse_public_key(&pk, (const uint8_t *)OTA_PUBLIC_KEY, strlen(OTA_PUBLIC_KEY) + 1)) {
    err = "Schluessel defekt";
  } else if (mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, hash, 32, sig, sigLen)) {
    err = "Signatur falsch";
  } else {
    ok = true;
  }
  mbedtls_pk_free(&pk);
  return ok;
}

static bool install(const String &url, size_t size, const uint8_t expected[32], const String &version,
                    const char *shaHex, const String &sig, String &err) {
  WiFiClientSecure client;
  client.setCACertBundle(x509_crt_bundle_start);
  client.setTimeout(20);
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);  // GitHub leitet auf den Speicher um
  http.setUserAgent(net::USER_AGENT);
  http.setTimeout(20000);
  if (!http.begin(client, url)) { err = "URL"; return false; }
  int code = http.GET();
  if (code != 200) { err = "Download " + String(code); http.end(); return false; }
  int len = http.getSize();
  if (len > 0 && (size_t)len != size) { err = "Groesse passt nicht"; http.end(); return false; }
  if (!Update.begin(size)) { err = "kein Platz: " + String(Update.errorString()); http.end(); return false; }
  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts(&sha, 0);
  WiFiClient *stream = http.getStreamPtr();
  static uint8_t buf[2048];
  size_t done = 0;
  uint32_t lastData = millis();
  while (done < size) {
    size_t avail = stream->available();
    if (!avail) {
      if (!http.connected() || millis() - lastData > 20000) break;
      delay(5);
      continue;
    }
    int n = stream->readBytes(buf, avail > sizeof(buf) ? sizeof(buf) : avail);
    if (n <= 0) continue;
    lastData = millis();
    mbedtls_sha256_update(&sha, buf, n);
    if (Update.write(buf, n) != (size_t)n) { err = "Schreiben: " + String(Update.errorString()); break; }
    done += n;
  }
  http.end();
  uint8_t hash[32];
  mbedtls_sha256_finish(&sha, hash);
  mbedtls_sha256_free(&sha);
  if (done != size) {
    if (!err.length()) err = "Download unvollstaendig";
    Update.abort();
    return false;
  }
  if (memcmp(hash, expected, 32)) { err = "Pruefsumme falsch"; Update.abort(); return false; }
  if (!verifySignature(version, shaHex, sig, err)) { Update.abort(); return false; }
  if (!Update.end(true)) { err = "Abschluss: " + String(Update.errorString()); return false; }
  return true;
}

Result check(bool force, void (*progress)(const String &version)) {
  Result r;
  if (cfg::getInt("ota_auto", 1) == 0 && !force) return r;
  time_t now = time(nullptr);
  if (!force && now - cfg::getInt("ota_at", 0) < 86400) return r;
  cfg::setInt("ota_at", (int)now);
  JsonDocument doc;
  int code = -1;
  for (int attempt = 0; attempt < 3 && code != 200; attempt++) {  // Mobilfunk-Hotspots haken gern kurz
    if (attempt) delay(2000);
    doc.clear();
    code = net::getJson(cfg::get("ota_url", DEFAULT_MANIFEST), doc);
  }
  if (code != 200) {
    r.error = "Versionsdatei " + String(code);
    cfg::setInt("ota_at", 0);  // beim naechsten Lauf erneut versuchen statt erst morgen
    return r;
  }
  r.checked = true;
  r.version = (const char *)(doc["version"] | "");
  if (!r.version.length() || cmpVersion(r.version, EIGEN_FIRMWARE_VERSION) <= 0) return r;
  r.available = true;
  uint8_t expected[32];
  const char *shaHex = doc["sha256"] | "";
  if (!hexToBytes(shaHex, expected, 32)) { r.error = "Versionsdatei fehlerhaft"; return r; }
  // Signatur vor dem Download pruefen: spart den Download bei manipulierter Versionsdatei
  String err;
  if (!verifySignature(r.version, shaHex, doc["sig"] | "", err)) {
    r.error = err;
    cfg::set("ota_last", "Update " + r.version + ": " + err);
    return r;
  }
  if (progress) progress(r.version);
  r.installed = install(doc["url"] | "", doc["size"] | 0, expected, r.version, shaHex, doc["sig"] | "", err);
  r.error = err;
  cfg::set("ota_last", r.installed ? "Version " + r.version + " eingespielt" : "Update " + r.version + ": " + err);
  return r;
}

bool pendingVerify() {
  esp_ota_img_states_t st;
  const esp_partition_t *p = esp_ota_get_running_partition();
  return esp_ota_get_state_partition(p, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY;
}

void markHealthy() {
  if (pendingVerify()) {
    esp_ota_mark_app_valid_cancel_rollback();
    cfg::set("ota_last", String("Version ") + EIGEN_FIRMWARE_VERSION + " l\xC3\xA4uft");
  }
}

}  // namespace ota
