#include "providers/CursorProvider.h"
#include "providers/HttpJson.h"
#include <ArduinoJson.h>

UsageSnapshot CursorProvider::fetch(const ProviderConfig &cfg, bool tls) {
  UsageSnapshot out; out.provider="Cursor"; out.primary.label="MONTH"; out.secondary.label="TODAY";
  if (!cfg.enabled) { out.status="disabled"; return out; }
  JsonDocument d; String error;
  // Cursor documents this Admin API for team admins. Individual subscriptions do
  // not currently have an official usage API; do not paste browser cookies here.
  String body="{\"startDate\":\"2020-01-01\",\"endDate\":\"2099-12-31\"}";
  if (!postJson(cfg.endpoint, cfg, tls, body, d, error)) { out.status=error; return out; }
  JsonArray rows = d["data"].as<JsonArray>();
  double spend=0; int requests=0;
  for (JsonObject row: rows) { spend += row["spend"] | row["spendCents"] | 0.0; requests += row["totalRequests"] | row["requests"] | 0; }
  out.primary.usedPercent = constrain((float)spend, 0.0f, 100.0f); out.primary.resetText=String(requests)+" requests";
  out.ok=true; out.status="team admin API"; return out;
}
