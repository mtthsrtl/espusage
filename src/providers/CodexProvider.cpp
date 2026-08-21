#include "providers/CodexProvider.h"
#include "providers/HttpJson.h"
#include <ArduinoJson.h>

UsageSnapshot CodexProvider::fetch(const ProviderConfig &cfg, bool tls) {
  UsageSnapshot out; out.provider="Codex"; out.primary.label="CODEX WEEKLY";
  if (!cfg.enabled) { out.status="disabled"; return out; }
  if (!cfg.endpoint.length()) { out.status="No official consumer Usage API; configure a compatible proxy"; return out; }
  JsonDocument d; String error;
  if (!getJson(cfg.endpoint, cfg, tls, d, error)) { out.status=error; return out; }
  // Adapter contract documented in README. This intentionally does not hard-code
  // ChatGPT's undocumented first-party web endpoints.
  // The display intentionally exposes only the weekly Codex allowance.
  // Prefer the adapter's secondary/week object, while accepting a simpler
  // weekly object and the legacy primary-only contract.
  JsonVariant week = d["weekly"];
  if (week.isNull()) week = d["secondary"];
  if (week.isNull()) week = d["primary"];
  out.primary.usedPercent=week["used_percent"] | -1.0f;
  out.primary.resetText=String((const char*)(week["reset_text"] | ""));
  out.credits=d["credits"] | -1.0f; out.plan=String((const char*)(d["plan"] | ""));
  out.ok=out.primary.usedPercent >= 0; out.status=out.ok ? "online" : "missing fields"; return out;
}

