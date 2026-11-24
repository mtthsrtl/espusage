#include "providers/HttpJson.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

static const char *requestLabel(const String &url) {
  if (url.indexOf("chatgpt.com") >= 0) return "codex";
  if (url.indexOf("claude.ai") >= 0 || url.indexOf("api.anthropic.com") >= 0 || url.indexOf("platform.claude.com") >= 0)
    return "claude";
  if (url.indexOf("cursor.com") >= 0 || url.indexOf("cursor.sh") >= 0) return "cursor";
  return "adapter";
}

static bool isTransportError(int code) {
  return code == HTTPC_ERROR_CONNECTION_REFUSED || code == HTTPC_ERROR_CONNECTION_LOST ||
         code == HTTPC_ERROR_NOT_CONNECTED || code == HTTPC_ERROR_READ_TIMEOUT ||
         code == HTTPC_ERROR_SEND_PAYLOAD_FAILED || code == HTTPC_ERROR_SEND_HEADER_FAILED ||
         code == HTTPC_ERROR_TOO_LESS_RAM;
}

static bool request(const String &method, const String &url, const ProviderConfig &cfg, bool verifyTls,
                    const String &body, JsonDocument &doc, String &error) {
  (void)verifyTls;
  if (!url.startsWith("https://")) { error = "HTTPS required"; return false; }
  const char *label = requestLabel(url);
  const uint8_t maxAttempts = 3;

  for (uint8_t attempt = 1; attempt <= maxAttempts; ++attempt) {
    WiFiClientSecure secureClient;
    // ESP32 has no maintained CA bundle in this firmware. Strict TLS needs a
    // custom CA in a future release; never downgrade a public request to HTTP.
    secureClient.setInsecure();
    secureClient.setHandshakeTimeout(20);
    HTTPClient http;
    if (!http.begin(secureClient, url)) { error = "connection setup failed"; return false; }
    // Fail-fast on refused/unreachable hosts so one provider cannot starve the web UI.
    http.setConnectTimeout(8000);
    http.setTimeout(15000);
    http.setReuse(false);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    const bool claudeOauth = url.indexOf("api.anthropic.com/api/oauth/") >= 0;
    http.setUserAgent(claudeOauth ? "claude-code/2.0.0" : "ESPUsage/1.0");
    http.addHeader("Accept", "application/json");
    // Avoid gzip/br bodies — ESP HTTPClient + ArduinoJson stream parse often
    // yields DeserializationError::InvalidInput on compressed Anthropic replies.
    http.addHeader("Accept-Encoding", "identity");
    http.addHeader("Connection", "close");
    if (url.indexOf("api2.cursor.sh/aiserver.v1.DashboardService/") >= 0)
      http.addHeader("Connect-Protocol-Version", "1");
    if (url.startsWith("https://cursor.com/api/dashboard/")) {
      http.addHeader("Origin", "https://cursor.com");
      http.addHeader("Referer", "https://cursor.com/dashboard");
    }
    if (url.indexOf("claude.ai") >= 0) {
      http.addHeader("Origin", "https://claude.ai");
      http.addHeader("Referer", "https://claude.ai/settings/usage");
    }
    if (claudeOauth) {
      http.addHeader("anthropic-beta", "oauth-2025-04-20");
      http.addHeader("anthropic-version", "2023-06-01");
    }
    if (cfg.token.length()) http.addHeader("Authorization", "Bearer " + cfg.token);
    if (cfg.accountId.length() && url.indexOf("chatgpt.com") >= 0) http.addHeader("ChatGPT-Account-Id", cfg.accountId);
    if (cfg.session.length()) http.addHeader("Cookie", cfg.session);
    int code;
    if (method == "POST") { http.addHeader("Content-Type", "application/json"); code = http.POST(body); }
    else code = http.GET();
    if (code < 0) {
      String detail = HTTPClient::errorToString(code);
      error = "transport " + String(code) + ": " + detail;
      Serial.printf("[http][%s] Transport error %d (%s), attempt %u/%u heap=%u\n",
                    label, code, detail.c_str(), attempt, maxAttempts, (unsigned)ESP.getFreeHeap());
      http.end();
      if (attempt < maxAttempts && isTransportError(code)) {
        delay(200u * attempt * attempt);
        yield();
        continue;
      }
      return false;
    }
    if (code < 200 || code >= 300) {
      error = "HTTP " + String(code);
      Serial.printf("[http][%s] HTTP %d\n", label, code);
      http.end();
      return false;
    }
    // Prefer a full buffered body over streaming; chunked Anthropic responses
    // frequently confuse ArduinoJson's stream reader on ESP32.
    String payload = http.getString();
    http.end();
    payload.trim();
    if (!payload.length()) {
      error = "empty JSON body";
      Serial.printf("[http][%s] Empty body on HTTP %d, attempt %u/%u\n", label, code, attempt, maxAttempts);
      if (attempt < maxAttempts) { delay(250); yield(); continue; }
      return false;
    }
    doc.clear();
    DeserializationError parse = deserializeJson(doc, payload);
    if (parse) {
      error = String("invalid JSON: ") + parse.c_str();
      String head = payload.substring(0, payload.length() < 80 ? payload.length() : 80);
      head.replace('\n', ' ');
      Serial.printf("[http][%s] JSON parse %s, bytes=%u, head=%.80s\n",
                    label, parse.c_str(), (unsigned)payload.length(), head.c_str());
      payload = "";
      if (attempt < maxAttempts) { delay(250); yield(); continue; }
      return false;
    }
    payload = "";
    if (attempt > 1) Serial.printf("[http][%s] Retry succeeded\n", label);
    // Let WiFi/TCP and the local web server breathe between HTTPS round-trips.
    yield();
    delay(40);
    yield();
    return true;
  }
  return false;
}
bool getJson(const String &u, const ProviderConfig &c, bool t, JsonDocument &d, String &e) { return request("GET",u,c,t,"",d,e); }
bool postJson(const String &u, const ProviderConfig &c, bool t, const String &b, JsonDocument &d, String &e) { return request("POST",u,c,t,b,d,e); }
