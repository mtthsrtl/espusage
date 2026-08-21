#include "providers/HttpJson.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

static bool request(const String &method, const String &url, const ProviderConfig &cfg, bool verifyTls,
                    const String &body, JsonDocument &doc, String &error) {
  WiFiClientSecure client;
  // ESP32 has no maintained CA bundle in this firmware. Strict TLS needs a custom CA
  // in a future release; never silently downgrade to plain HTTP.
  if (verifyTls) client.setInsecure(); else client.setInsecure();
  HTTPClient http;
  if (!url.startsWith("https://")) { error = "HTTPS required"; return false; }
  if (!http.begin(client, url)) { error = "connection setup failed"; return false; }
  http.setTimeout(15000); http.addHeader("Accept", "application/json");
  if (url.indexOf("api2.cursor.sh/aiserver.v1.DashboardService/") >= 0)
    http.addHeader("Connect-Protocol-Version", "1");
  if (cfg.token.length()) http.addHeader("Authorization", "Bearer " + cfg.token);
  if (cfg.session.length()) http.addHeader("Cookie", cfg.session);
  int code;
  if (method == "POST") { http.addHeader("Content-Type", "application/json"); code = http.POST(body); }
  else code = http.GET();
  if (code < 200 || code >= 300) { error = "HTTP " + String(code); http.end(); return false; }
  DeserializationError parse = deserializeJson(doc, http.getStream());
  http.end();
  if (parse) { error = String("invalid JSON: ") + parse.c_str(); return false; }
  return true;
}
bool getJson(const String &u, const ProviderConfig &c, bool t, JsonDocument &d, String &e) { return request("GET",u,c,t,"",d,e); }
bool postJson(const String &u, const ProviderConfig &c, bool t, const String &b, JsonDocument &d, String &e) { return request("POST",u,c,t,b,d,e); }

