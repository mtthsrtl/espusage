#include "providers/CodexProvider.h"
#include "providers/HttpJson.h"
#include <ArduinoJson.h>

UsageSnapshot CodexProvider::fetch(const ProviderConfig &cfg, bool tls) {
  UsageSnapshot out; out.provider="Codex"; out.primary.label="5 HOUR"; out.secondary.label="WEEK";
  if (!cfg.enabled) { out.status="disabled"; return out; }
  if (!cfg.endpoint.length()) { out.status="No official consumer Usage API; configure a compatible proxy"; return out; }
  JsonDocument d; String error;
  if (!getJson(cfg.endpoint, cfg, tls, d, error)) { out.status=error; return out; }
  // Adapter contract documented in README. This intentionally does not hard-code
  // ChatGPT's undocumented first-party web endpoints.
  out.primary.usedPercent=d["primary"]["used_percent"] | -1.0f;
  out.primary.resetText=String((const char*)(d["primary"]["reset_text"] | ""));
  out.secondary.usedPercent=d["secondary"]["used_percent"] | -1.0f;
  out.secondary.resetText=String((const char*)(d["secondary"]["reset_text"] | ""));
  out.credits=d["credits"] | -1.0f; out.plan=String((const char*)(d["plan"] | ""));
  out.ok=out.primary.usedPercent >= 0; out.status=out.ok ? "online" : "missing fields"; return out;
}

