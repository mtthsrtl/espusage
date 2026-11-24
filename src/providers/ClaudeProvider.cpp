#include "providers/ClaudeProvider.h"
#include "providers/HttpJson.h"
#include <ArduinoJson.h>
#include <time.h>

static String normalizeSessionKey(String value) {
  value.trim();
  if (value.startsWith("sessionKey=")) value.remove(0, 11);
  int cut = value.indexOf(';');
  if (cut >= 0) value = value.substring(0, cut);
  value.trim();
  return value;
}

static time_t parseIsoUtc(const char *text) {
  if (!text || !text[0]) return 0;
  int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
  if (sscanf(text, "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &minute, &second) < 6) return 0;
  struct tm t = {};
  t.tm_year = year - 1900;
  t.tm_mon = month - 1;
  t.tm_mday = day;
  t.tm_hour = hour;
  t.tm_min = minute;
  t.tm_sec = second;
  // Temporarily switch to UTC so mktime interprets the Claude resets_at stamp correctly.
  char *previous = getenv("TZ");
  String previousTz = previous ? previous : "";
  setenv("TZ", "UTC0", 1);
  tzset();
  time_t utc = mktime(&t);
  if (previousTz.length()) setenv("TZ", previousTz.c_str(), 1);
  else unsetenv("TZ");
  tzset();
  return utc > 0 ? utc : 0;
}

static void readUtilization(JsonVariant source, UsageWindow &target, uint32_t windowSeconds) {
  if (source.isNull()) return;
  target.usedPercent = source["utilization"] | -1.0f;
  target.windowSeconds = windowSeconds;
  const char *resetAt = source["resets_at"] | "";
  time_t reset = parseIsoUtc(resetAt);
  time_t now = time(nullptr);
  if (reset > 1700000000 && now > 1700000000 && reset > now) {
    uint32_t remaining = (uint32_t)(reset - now);
    uint32_t days = remaining / 86400, hours = (remaining % 86400) / 3600, minutes = (remaining % 3600) / 60;
    target.resetText = days ? "reset in " + String(days) + "d " + String(hours) + "h"
                            : "reset in " + String(hours) + "h " + String(minutes) + "m";
    if (windowSeconds && remaining <= windowSeconds)
      target.elapsedPercent = constrain(100.0f - (100.0f * remaining / windowSeconds), 0.0f, 100.0f);
  } else {
    target.resetText = "reset time unavailable";
  }
}

static String resolveOrgId(const ProviderConfig &cfg, bool tls, String &error) {
  if (cfg.accountId.length()) return cfg.accountId;
  JsonDocument orgs;
  ProviderConfig request = cfg;
  request.token = "";
  request.session = "sessionKey=" + normalizeSessionKey(cfg.token);
  if (!getJson("https://claude.ai/api/organizations", request, tls, orgs, error)) return "";
  JsonArray list = orgs.as<JsonArray>();
  if (list.isNull()) {
    // Some responses wrap the list.
    list = orgs["organizations"].as<JsonArray>();
  }
  String chatOrg, fallback;
  for (JsonObject org : list) {
    String uuid = String((const char *)(org["uuid"] | ""));
    if (!uuid.length()) continue;
    if (!fallback.length()) fallback = uuid;
    JsonArray caps = org["capabilities"].as<JsonArray>();
    bool chat = false;
    for (JsonVariant cap : caps) {
      String name = cap.as<String>();
      if (name == "chat" || name.indexOf("claude") >= 0) { chat = true; break; }
    }
    if (chat) { chatOrg = uuid; break; }
  }
  if (chatOrg.length()) return chatOrg;
  if (fallback.length()) return fallback;
  error = "Claude organization missing";
  return "";
}

UsageSnapshot ClaudeProvider::fetch(const ProviderConfig &cfg, bool tls) {
  UsageSnapshot out;
  out.provider = "Claude";
  out.primary.label = "5-HOUR LIMIT";
  out.secondary.label = "WEEKLY LIMIT";
  if (!cfg.enabled) { out.status = "disabled"; return out; }
  String sessionKey = normalizeSessionKey(cfg.token);
  if (!sessionKey.length()) { out.status = "Claude sessionKey missing"; return out; }

  ProviderConfig request = cfg;
  request.token = "";
  request.session = "sessionKey=" + sessionKey;

  String error;
  bool discoveredOrg = !cfg.accountId.length();
  String orgId = resolveOrgId(request, tls, error);
  if (!orgId.length()) {
    out.status = error.length() ? error : "Claude organization missing";
    return out;
  }
  // When accountId was empty, stash the resolved org UUID here so main can persist it.
  if (discoveredOrg) out.updated = orgId;

  String url = "https://claude.ai/api/organizations/" + orgId + "/usage";
  JsonDocument d;
  if (!getJson(url, request, tls, d, error)) { out.status = error; return out; }

  readUtilization(d["five_hour"], out.primary, 5 * 3600);
  readUtilization(d["seven_day"], out.secondary, 7 * 86400);
  // Newer settings pages also expose a limits array — fill gaps only.
  JsonArray limits = d["limits"].as<JsonArray>();
  for (JsonObject limit : limits) {
    String type = String((const char *)(limit["type"] | limit["id"] | ""));
    type.toLowerCase();
    if ((type.indexOf("five") >= 0 || type.indexOf("5_hour") >= 0 || type.indexOf("5h") >= 0) && out.primary.usedPercent < 0)
      readUtilization(limit, out.primary, 5 * 3600);
    if ((type.indexOf("seven") >= 0 || type.indexOf("week") >= 0 || type.indexOf("7_day") >= 0) && out.secondary.usedPercent < 0)
      readUtilization(limit, out.secondary, 7 * 86400);
  }

  JsonVariant extra = d["extra_usage"];
  if (!extra.isNull() && (extra["is_enabled"] | false)) {
    float used = extra["used_credits"] | -1.0f;
    if (used >= 0) out.credits = used;
  }

  const char *plan = d["plan_type"] | d["plan"] | "";
  if (plan[0]) out.plan = plan;
  else if ((d["five_hour"].isNull() == false) || (d["seven_day"].isNull() == false)) out.plan = "Claude";

  out.ok = out.primary.usedPercent >= 0 || out.secondary.usedPercent >= 0;
  out.status = out.ok ? "online (Claude session)" : "usage fields missing";
  return out;
}
