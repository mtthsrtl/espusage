#include "providers/HttpJson.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

static const char *requestLabel(const String &url) {
  if (url.indexOf("chatgpt.com") >= 0) return "codex";
  if (url.indexOf("cursor.com") >= 0 || url.indexOf("cursor.sh") >= 0) return "cursor";
  return "adapter";
}

static bool request(const String &method, const String &url, const ProviderConfig &cfg, bool verifyTls,
                    const String &body, JsonDocument &doc, String &error) {
  (void)verifyTls;
  if (!url.startsWith("https://")) { error = "HTTPS required"; return false; }
  const char *label = requestLabel(url);

  for (uint8_t attempt = 1; attempt <= 2; ++attempt) {
    WiFiClientSecure secureClient;
    // ESP32 has no maintained CA bundle in this firmware. Strict TLS needs a
    // custom CA in a future release; never downgrade a public request to HTTP.
    secureClient.setInsecure();
    HTTPClient http;
    if (!http.begin(secureClient, url)) { error = "connection setup failed"; return false; }
    http.setConnectTimeout(12000); http.setTimeout(20000); http.setReuse(false);
    http.setUserAgent("ESPUsage/1.0"); http.addHeader("Accept", "application/json");
    http.addHeader("Connection", "close");
    if (url.indexOf("api2.cursor.sh/aiserver.v1.DashboardService/") >= 0)
      http.addHeader("Connect-Protocol-Version", "1");
    if (url.startsWith("https://cursor.com/api/dashboard/")) {
      http.addHeader("Origin", "https://cursor.com");
      http.addHeader("Referer", "https://cursor.com/dashboard");
    }
    if (cfg.token.length()) http.addHeader("Authorization", "Bearer " + cfg.token);
    if (cfg.accountId.length()) http.addHeader("ChatGPT-Account-Id", cfg.accountId);
    if (cfg.session.length()) http.addHeader("Cookie", cfg.session);
    int code;
    if (method == "POST") { http.addHeader("Content-Type", "application/json"); code = http.POST(body); }
    else code = http.GET();
    if (code < 0) {
      String detail = HTTPClient::errorToString(code);
      error = "transport " + String(code) + ": " + detail;
      Serial.printf("[http][%s] Transport error %d (%s), attempt %u/2\n", label, code, detail.c_str(), attempt);
      http.end();
      if (attempt == 1) { delay(350); continue; }
      return false;
    }
    if (code < 200 || code >= 300) {
      error = "HTTP " + String(code);
      Serial.printf("[http][%s] HTTP %d\n", label, code);
      http.end();
      return false;
    }
    doc.clear();
    DeserializationError parse = deserializeJson(doc, http.getStream());
    http.end();
    if (parse) { error = String("invalid JSON: ") + parse.c_str(); return false; }
    if (attempt > 1) Serial.printf("[http][%s] Retry succeeded\n", label);
    return true;
  }
  return false;
}
bool getJson(const String &u, const ProviderConfig &c, bool t, JsonDocument &d, String &e) { return request("GET",u,c,t,"",d,e); }
bool postJson(const String &u, const ProviderConfig &c, bool t, const String &b, JsonDocument &d, String &e) { return request("POST",u,c,t,b,d,e); }

