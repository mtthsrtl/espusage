#include "providers/ClaudeProvider.h"
#include "providers/HttpJson.h"
#include <ArduinoJson.h>
#include <time.h>

static String normalizeToken(String value) {
  value.trim();
  if (value.startsWith("Bearer ")) value.remove(0, 7);
  if (value.startsWith("sessionKey=")) value.remove(0, 11);
  int cut = value.indexOf(';');
  if (cut >= 0) value = value.substring(0, cut);
  value.trim();
  return value;
}

static bool isOAuthAccessToken(const String &token) {
  return token.startsWith("sk-ant-oat") || token.startsWith("sk-ant-api");
}

static bool isSessionCookieToken(const String &token) {
  return token.startsWith("sk-ant-sid");
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
  } else if (target.usedPercent >= 0) {
    target.resetText = "reset time unavailable";
  }
}

static void fillFromUsageDocument(JsonDocument &d, UsageSnapshot &out, const char *statusOk) {
  readUtilization(d["five_hour"], out.primary, 5 * 3600);
  readUtilization(d["seven_day"], out.secondary, 7 * 86400);
  JsonArray limits = d["limits"].as<JsonArray>();
  for (JsonObject limit : limits) {
    String type = String((const char *)(limit["type"] | limit["id"] | limit["kind"] | ""));
    type.toLowerCase();
    if ((type.indexOf("five") >= 0 || type.indexOf("5_hour") >= 0 || type.indexOf("5h") >= 0 || type.indexOf("session") >= 0) &&
        out.primary.usedPercent < 0)
      readUtilization(limit, out.primary, 5 * 3600);
    if ((type.indexOf("seven") >= 0 || type.indexOf("week") >= 0 || type.indexOf("7_day") >= 0) &&
        out.secondary.usedPercent < 0)
      readUtilization(limit, out.secondary, 7 * 86400);
  }
  JsonVariant extra = d["extra_usage"];
  if (!extra.isNull() && (extra["is_enabled"] | false)) {
    float used = extra["used_credits"] | -1.0f;
    if (used >= 0) out.credits = used;
  }
  const char *plan = d["plan_type"] | d["plan"] | "";
  if (plan[0]) out.plan = plan;
  out.ok = out.primary.usedPercent >= 0 || out.secondary.usedPercent >= 0;
  out.status = out.ok ? statusOk : "usage fields missing";
}

static String planFromProfile(JsonDocument &profile) {
  JsonObject account = profile["account"].as<JsonObject>();
  JsonObject organization = profile["organization"].as<JsonObject>();
  if (!account.isNull()) {
    if (account["has_claude_max"] | false) return "MAX";
    if (account["has_claude_pro"] | false) return "PRO";
  }
  String orgType = String((const char *)(organization["organization_type"] | ""));
  orgType.toLowerCase();
  if (orgType.indexOf("max") >= 0) return "MAX";
  if (orgType.indexOf("pro") >= 0) return "PRO";
  if (orgType.indexOf("team") >= 0) return "TEAM";
  if (orgType.indexOf("enterprise") >= 0) return "ENT";
  if (orgType.length()) {
    orgType.replace("claude_", "");
    orgType.replace("_", " ");
    orgType.toUpperCase();
    return orgType;
  }
  return "";
}

static String cachedClaudePlan;
static uint32_t cachedClaudePlanAtMs = 0;
static constexpr uint32_t CLAUDE_PLAN_CACHE_MS = 6UL * 60UL * 60UL * 1000UL;

static UsageSnapshot fetchOAuthUsage(const ProviderConfig &cfg, bool tls, const String &accessToken) {
  UsageSnapshot out;
  out.provider = "Claude";
  out.primary.label = "5-HOUR LIMIT";
  out.secondary.label = "WEEKLY LIMIT";
  ProviderConfig request = cfg;
  request.token = accessToken;
  request.session = "";
  JsonDocument d;
  String error;
  if (!getJson("https://api.anthropic.com/api/oauth/usage", request, tls, d, error)) {
    out.status = error;
    if (error == "HTTP 401" || error == "HTTP 403")
      out.status = error + " (Claude OAuth token invalid/expired - re-run PC helper)";
    return out;
  }
  fillFromUsageDocument(d, out, "online (Claude OAuth)");
  if (cachedClaudePlan.length() && cachedClaudePlanAtMs &&
      (millis() - cachedClaudePlanAtMs) < CLAUDE_PLAN_CACHE_MS) {
    out.plan = cachedClaudePlan;
    return out;
  }
  JsonDocument profile;
  String profileError;
  if (getJson("https://api.anthropic.com/api/oauth/profile", request, tls, profile, profileError)) {
    String plan = planFromProfile(profile);
    if (plan.length()) {
      out.plan = plan;
      cachedClaudePlan = plan;
      cachedClaudePlanAtMs = millis();
    }
    String orgId = String((const char *)(profile["organization"]["uuid"] | ""));
    if (orgId.length() && !cfg.accountId.length()) out.updated = orgId;
  } else if (cachedClaudePlan.length()) {
    out.plan = cachedClaudePlan;
  } else if (!out.plan.length() || out.plan == "Claude") {
    // Keep usage bars even if profile fails; leave badge empty rather than "CLAUDE".
    out.plan = "";
  }
  return out;
}

UsageSnapshot ClaudeProvider::fetch(const ProviderConfig &cfg, bool tls) {
  UsageSnapshot out;
  out.provider = "Claude";
  out.primary.label = "5-HOUR LIMIT";
  out.secondary.label = "WEEKLY LIMIT";
  if (!cfg.enabled) { out.status = "disabled"; return out; }

  String token = normalizeToken(cfg.token);
  if (!token.length()) { out.status = "Claude OAuth token missing"; return out; }

  // Browser sessionKey cookies are blocked by Cloudflare for non-browser TLS
  // (ESP and PowerShell). Prefer Claude Code OAuth against api.anthropic.com.
  if (isSessionCookieToken(token)) {
    out.status = "HTTP 403 (sessionKey blocked by Cloudflare - use Claude Code OAuth via PC helper)";
    return out;
  }

  if (isOAuthAccessToken(token) || !isSessionCookieToken(token)) {
    return fetchOAuthUsage(cfg, tls, token);
  }

  out.status = "unsupported Claude credential";
  return out;
}
