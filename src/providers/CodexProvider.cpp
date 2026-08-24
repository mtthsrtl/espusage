#include "providers/CodexProvider.h"
#include "providers/HttpJson.h"
#include <ArduinoJson.h>
#include <time.h>

static String resetText(JsonVariant window) {
  uint32_t remaining=window["reset_after_seconds"] | 0;
  uint32_t resetAt=window["reset_at"] | 0;
  time_t now=time(nullptr);if(!remaining&&resetAt&&now>1700000000&&resetAt>(uint32_t)now)remaining=resetAt-(uint32_t)now;
  if(!remaining)return "reset time unavailable";
  uint32_t days=remaining/86400,hours=(remaining%86400)/3600,minutes=(remaining%3600)/60;
  return days?"reset in "+String(days)+"d "+String(hours)+"h":"reset in "+String(hours)+"h "+String(minutes)+"m";
}
static void readWindow(JsonVariant source, UsageWindow &target) {
  target.usedPercent=source["used_percent"] | -1.0f;target.resetText=resetText(source);
  uint32_t windowSeconds=source["limit_window_seconds"]|0;
  uint32_t remaining=source["reset_after_seconds"]|0;
  uint32_t resetAt=source["reset_at"]|0;
  time_t now=time(nullptr);
  if(!remaining&&resetAt&&now>1700000000&&resetAt>(uint32_t)now)remaining=resetAt-(uint32_t)now;
  target.windowSeconds=windowSeconds;
  if(windowSeconds&&remaining<=windowSeconds)target.elapsedPercent=constrain(100.0f-(100.0f*remaining/windowSeconds),0.0f,100.0f);
}

UsageSnapshot CodexProvider::fetch(const ProviderConfig &cfg, bool tls) {
  UsageSnapshot out; out.provider="Codex"; out.primary.label="5-HOUR LIMIT"; out.secondary.label="WEEKLY LIMIT";
  if (!cfg.enabled) { out.status="disabled"; return out; }
  bool direct=cfg.endpoint.length()==0;
  if (direct && !cfg.token.length()) { out.status="Codex access_token missing"; return out; }
  String url=direct?"https://chatgpt.com/backend-api/wham/usage":cfg.endpoint;
  JsonDocument d; String error;
  if (!getJson(url, cfg, tls, d, error)) { out.status=error; return out; }
  if (direct) {
    JsonVariant first=d["rate_limit"]["primary_window"],second=d["rate_limit"]["secondary_window"];
    int firstSeconds=first["limit_window_seconds"] | 0,secondSeconds=second["limit_window_seconds"] | 0;
    if(!first.isNull()){if(firstSeconds>6*3600)readWindow(first,out.secondary);else readWindow(first,out.primary);}
    if(!second.isNull()){if(secondSeconds>6*3600)readWindow(second,out.secondary);else readWindow(second,out.primary);}
    out.plan=String((const char*)(d["plan_type"] | ""));
    out.ok=out.primary.usedPercent>=0||out.secondary.usedPercent>=0;out.status=out.ok?"online (Codex token)":"usage fields missing";return out;
  }
  JsonVariant primary=d["primary"];
  JsonVariant weekly=d["weekly"];if(weekly.isNull())weekly=d["secondary"];
  out.primary.usedPercent=primary["used_percent"] | -1.0f;out.primary.elapsedPercent=primary["elapsed_percent"] | -1.0f;out.primary.windowSeconds=primary["window_seconds"] | 0;out.primary.resetText=String((const char*)(primary["reset_text"] | ""));
  out.secondary.usedPercent=weekly["used_percent"] | -1.0f;out.secondary.elapsedPercent=weekly["elapsed_percent"] | -1.0f;out.secondary.windowSeconds=weekly["window_seconds"] | 0;out.secondary.resetText=String((const char*)(weekly["reset_text"] | ""));
  if(out.secondary.usedPercent<0){out.secondary=out.primary;out.primary.usedPercent=-1;out.primary.resetText="";}
  out.credits=d["credits"] | -1.0f;out.plan=String((const char*)(d["plan"] | ""));
  out.ok=out.primary.usedPercent>=0||out.secondary.usedPercent>=0;out.status=out.ok?"online (adapter)":"missing fields";return out;
}

