#include "providers/CursorProvider.h"
#include "providers/HttpJson.h"
#include <ArduinoJson.h>
#include <mbedtls/base64.h>
#include <stdlib.h>
#include <time.h>

// UNDOCUMENTED CURSOR WEB API. Mirrors E:\Cursor_Usage's read-only flow.
// Cursor does not guarantee this endpoint or schema; it may change without notice.
static String normalizeSessionToken(String token) {
  token.trim();
  const String prefix = "WorkosCursorSessionToken=";
  if (token.startsWith(prefix)) token.remove(0, prefix.length());
  token.replace("%3A%3A", "::"); token.replace("%3a%3a", "::");
  if (token.indexOf("::") >= 0) return token;
  int first=token.indexOf('.'),second=first<0?-1:token.indexOf('.',first+1);
  if(first>0&&second>first){
    String encoded=token.substring(first+1,second);encoded.replace('-','+');encoded.replace('_','/');while(encoded.length()%4)encoded+='=';
    size_t outputLength=0;unsigned char decoded[768];
    if(mbedtls_base64_decode(decoded,sizeof(decoded)-1,&outputLength,(const unsigned char*)encoded.c_str(),encoded.length())==0){
      decoded[outputLength]=0;JsonDocument payload;if(deserializeJson(payload,decoded)==DeserializationError::Ok){String subject=String((const char*)(payload["sub"]|""));if(subject.length())return subject+"::"+token;}
    }
  }
  return token;
}
static String cookieFor(String token) {
  const String prefix = "WorkosCursorSessionToken=";
  token=normalizeSessionToken(token);
  token.replace("::", "%3A%3A");
  return prefix + token;
}
static time_t parseIsoUtc(const char *iso) {
  if (!iso || !*iso) return 0; struct tm t = {};
  if (sscanf(iso, "%d-%d-%dT%d:%d:%d", &t.tm_year, &t.tm_mon, &t.tm_mday, &t.tm_hour, &t.tm_min, &t.tm_sec) != 6) return 0;
  t.tm_year -= 1900; t.tm_mon -= 1;
  // Arduino-ESP32 2.x does not expose timegm(). The device clock is configured
  // as UTC, so mktime provides the required epoch value portably here.
  setenv("TZ", "UTC0", 1); tzset(); return mktime(&t);
}
static String remainingText(const char *iso) {
  time_t end=parseIsoUtc(iso), now=time(nullptr); if(end<=now||now<1700000000)return "reset time unavailable";
  uint32_t s=end-now,d=s/86400; s%=86400; uint32_t h=s/3600,m=(s%3600)/60;
  return d ? "reset in "+String(d)+"d "+String(h)+"h" : "reset in "+String(h)+"h "+String(m)+"m";
}
static float elapsedPercent(const char *startIso, const char *endIso) {
  time_t start=parseIsoUtc(startIso),end=parseIsoUtc(endIso),now=time(nullptr);
  if(start<=0||end<=start||now<1700000000)return -1.0f;
  float elapsed=100.0f*(float)(now-start)/(float)(end-start);
  return constrain(elapsed,0.0f,100.0f);
}
static uint32_t periodSeconds(const char *startIso, const char *endIso) {
  time_t start=parseIsoUtc(startIso),end=parseIsoUtc(endIso);
  return start>0&&end>start?(uint32_t)(end-start):0;
}
static float onDemandPercent(JsonVariant bucket) {
  if(bucket.isNull())return -1.0f;
  bool enabled=bucket["enabled"]|false;
  float limit=bucket["limit"]|-1.0f;
  float used=bucket["used"]|-1.0f;
  float remaining=bucket["remaining"]|-1.0f;
  if(limit>0){
    if(used<0&&remaining>=0)used=limit-remaining;
    if(used<0)used=0;
    return constrain(100.0f*used/limit,0.0f,100.0f);
  }
  return enabled?-1.0f:0.0f;
}

static void addOnDemandAmounts(JsonVariant bucket, UsageWindow &window) {
  if (bucket.isNull()) return;
  float limit = bucket["limit"] | -1.0f;
  float used = bucket["used"] | -1.0f;
  float remaining = bucket["remaining"] | -1.0f;
  if (used < 0 && limit >= 0 && remaining >= 0) used = limit - remaining;
  if (used < 0) return;
  // Cursor's personal usage-summary reports on-demand money in US cents.
  window.monetary = true;
  window.usedAmount = max(0.0f, used) / 100.0f;
  window.limitAmount = limit > 0 ? limit / 100.0f : -1.0f;
  window.currencySymbol = "$";
}

static uint64_t unsignedValue(JsonVariantConst value) {
  if (value.is<const char *>()) return strtoull(value.as<const char *>(), nullptr, 10);
  if (value.is<uint64_t>()) return value.as<uint64_t>();
  if (value.is<long long>()) return value.as<long long>() < 0 ? 0 : (uint64_t)value.as<long long>();
  return 0;
}

static float legacyCostCents(JsonVariantConst value) {
  if (value.is<float>() || value.is<double>() || value.is<int>()) return value.as<float>() * 100.0f;
  if (!value.is<const char *>()) return -1;
  String text = value.as<const char *>();
  text.replace("$", ""); text.replace(",", ""); text.trim();
  return text.length() ? text.toFloat() * 100.0f : -1;
}

static void fetchRecentUsage(const ProviderConfig &request, bool tls, RecentUsage30m &recent) {
  const uint16_t pageSize = 50;
  const uint8_t maxPages = 10;
  const uint64_t fiveMinutesMs = 5ULL * 60ULL * 1000ULL;
  time_t nowSeconds = time(nullptr);
  if (nowSeconds < 1700000000) {
    recent.status = "waiting for clock";
    Serial.println("[usage][cursor][30m] Waiting for NTP clock");
    return;
  }

  uint64_t endMs = (uint64_t)nowSeconds * 1000ULL;
  uint64_t startMs = endMs - 30ULL * 60ULL * 1000ULL;
  String modelNames[12]; uint16_t modelCounts[12] = {}; uint8_t modelCount = 0;
  String kindNames[8]; uint16_t kindCounts[8] = {}; uint8_t kindCount = 0;
  uint16_t missingTokenCalls = 0;
  bool anyCost = false, finished = false, firstPageOk = false;

  for (uint8_t page = 1; page <= maxPages; ++page) {
    String body;
    body.reserve(128);
    body = "{\"teamId\":0,\"startDate\":\"" + String(startMs) + "\",\"endDate\":\"" + String(endMs) +
           "\",\"page\":" + String(page) + ",\"pageSize\":" + String(pageSize) + "}";
    JsonDocument document; String error;
    if (!postJson("https://cursor.com/api/dashboard/get-filtered-usage-events", request, tls, body, document, error)) {
      recent.partial = firstPageOk;
      recent.status = firstPageOk ? "partial: " + error : error;
      Serial.printf("[usage][cursor][30m] Page %u failed: %s\n", page, error.c_str());
      break;
    }
    firstPageOk = true;
    JsonArray events = document["usageEventsDisplay"].as<JsonArray>();
    if (events.isNull()) events = document["usageEvents"].as<JsonArray>();
    if (events.isNull()) {
      finished = true;
      break;
    }

    for (JsonObject event : events) {
      uint64_t timestampMs = unsignedValue(event["timestamp"]);
      if (timestampMs && timestampMs < 100000000000ULL) timestampMs *= 1000ULL;
      if (timestampMs < startMs || timestampMs > endMs) continue;
      uint64_t bucketIndex = (timestampMs - startMs) / fiveMinutesMs;
      uint8_t bucket = (uint8_t)(bucketIndex > 5 ? 5 : bucketIndex);
      recent.calls++;
      recent.buckets[bucket].calls++;

      JsonObject tokenUsage = event["tokenUsage"].as<JsonObject>();
      if (!tokenUsage.isNull()) {
        bool hasTokenFields = !tokenUsage["inputTokens"].isNull() || !tokenUsage["outputTokens"].isNull() ||
                              !tokenUsage["cacheWriteTokens"].isNull() || !tokenUsage["cacheReadTokens"].isNull();
        uint64_t input = unsignedValue(tokenUsage["inputTokens"]);
        uint64_t output = unsignedValue(tokenUsage["outputTokens"]);
        uint64_t cacheWrite = unsignedValue(tokenUsage["cacheWriteTokens"]);
        uint64_t cacheRead = unsignedValue(tokenUsage["cacheReadTokens"]);
        uint64_t tokens = input + output + cacheWrite + cacheRead;
        if (hasTokenFields) {
          recent.tokenizedCalls++;
          recent.inputTokens += input; recent.outputTokens += output;
          recent.cacheWriteTokens += cacheWrite; recent.cacheReadTokens += cacheRead;
          recent.buckets[bucket].tokens += tokens;
        } else missingTokenCalls++;
        if (!tokenUsage["totalCents"].isNull()) {
          if (!anyCost) recent.costCents = 0;
          recent.costCents += tokenUsage["totalCents"].as<float>(); anyCost = true;
        }
      } else {
        missingTokenCalls++;
      }
      if (tokenUsage.isNull() && !event["usageBasedCosts"].isNull()) {
        float cost = legacyCostCents(event["usageBasedCosts"]);
        if (cost >= 0) { if (!anyCost) recent.costCents = 0; recent.costCents += cost; anyCost = true; }
      }

      String model = String((const char *)(event["model"] | ""));
      if (model.length()) {
        int found = -1;
        for (uint8_t i = 0; i < modelCount; ++i) if (modelNames[i] == model) { found = i; break; }
        if (found < 0 && modelCount < 12) { found = modelCount; modelNames[modelCount++] = model; }
        if (found >= 0) modelCounts[found]++;
      }
      String kind = String((const char *)(event["customSubscriptionName"] | ""));
      if (!kind.length()) kind = String((const char *)(event["kind"] | ""));
      if (kind.length()) {
        int found = -1;
        for (uint8_t i = 0; i < kindCount; ++i) if (kindNames[i] == kind) { found = i; break; }
        if (found < 0 && kindCount < 8) { found = kindCount; kindNames[kindCount++] = kind; }
        if (found >= 0) kindCounts[found]++;
      }
      if (event["maxMode"] | false) recent.maxModeCalls++;
    }

    bool hasNext = events.size() >= pageSize;
    JsonVariant paginationFlag = document["pagination"]["hasNextPage"];
    if (!paginationFlag.isNull()) hasNext = paginationFlag.as<bool>();
    if (!hasNext) { finished = true; break; }
    if (page == maxPages) recent.partial = true;
  }

  if (!firstPageOk) return;
  recent.available = true; recent.ready = true; recent.tokenData = recent.tokenizedCalls > 0;
  for (RecentUsageBucket &bucket : recent.buckets) bucket.valid = true;
  uint16_t bestCount = 0;
  for (uint8_t i = 0; i < modelCount; ++i) if (modelCounts[i] > bestCount) { bestCount = modelCounts[i]; recent.topModel = modelNames[i]; }
  bestCount = 0;
  for (uint8_t i = 0; i < kindCount; ++i) if (kindCounts[i] > bestCount) { bestCount = kindCounts[i]; recent.topKind = kindNames[i]; }
  if (!finished) recent.partial = true;
  if (recent.partial) recent.status = "partial event data";
  else if (!recent.calls) recent.status = "no activity";
  else if (recent.tokenizedCalls < recent.calls) recent.status = "tokens for " + String(recent.tokenizedCalls) + "/" + String(recent.calls) + " calls";
  else recent.status = "online";
  uint64_t totalTokens = recent.inputTokens + recent.outputTokens + recent.cacheWriteTokens + recent.cacheReadTokens;
  if (missingTokenCalls) Serial.printf("[usage][cursor][30m] Missing token fields for %u/%u calls\n", missingTokenCalls, recent.calls);
  Serial.printf("[usage][cursor][30m] calls=%u, tokenized=%u, max-mode=%u, tokens=%llu, pages=%s\n",
                recent.calls, recent.tokenizedCalls, recent.maxModeCalls, (unsigned long long)totalTokens, recent.partial ? "partial" : "complete");
}

UsageSnapshot CursorProvider::fetch(const ProviderConfig &cfg, bool tls) {
  UsageSnapshot out; out.provider="Cursor"; out.primary.label="CURSOR MODELS"; out.secondary.label="OTHER MODELS"; out.tertiary.label="ON DEMAND";
  if(!cfg.enabled){out.status="disabled";return out;} if(!cfg.token.length()){out.status="auth token missing";return out;}
  ProviderConfig request=cfg; request.token=""; request.session=cookieFor(cfg.token);
  JsonDocument d; String error; String url=cfg.endpoint.length()?cfg.endpoint:"https://cursor.com/api/usage-summary";
  if(!getJson(url,request,tls,d,error)){out.status=error;return out;}
  float autoUsed=d["individualUsage"]["plan"]["autoPercentUsed"]|-1.0f;
  float apiUsed=d["individualUsage"]["plan"]["apiPercentUsed"]|-1.0f;
  JsonVariant individualDemand=d["individualUsage"]["onDemand"];
  float demandUsed=onDemandPercent(individualDemand);
  if(demandUsed<0){
    JsonVariant teamDemand=d["teamUsage"]["onDemand"];
    demandUsed=onDemandPercent(teamDemand);
    addOnDemandAmounts(teamDemand,out.tertiary);
  } else addOnDemandAmounts(individualDemand,out.tertiary);
  if(demandUsed<0)demandUsed=0.0f;
  String reset=remainingText(d["billingCycleEnd"]|"");
  const char *cycleStart=d["billingCycleStart"]|"",*cycleEnd=d["billingCycleEnd"]|"";
  float elapsed=elapsedPercent(cycleStart,cycleEnd); uint32_t windowSeconds=periodSeconds(cycleStart,cycleEnd);
  out.primary.usedPercent=autoUsed; out.secondary.usedPercent=apiUsed; out.tertiary.usedPercent=demandUsed;
  out.primary.elapsedPercent=elapsed; out.secondary.elapsedPercent=elapsed; out.tertiary.elapsedPercent=elapsed;
  out.primary.windowSeconds=windowSeconds; out.secondary.windowSeconds=windowSeconds; out.tertiary.windowSeconds=windowSeconds;
  out.primary.resetText=reset; out.secondary.resetText=reset; out.tertiary.resetText=reset;
  out.plan=String((const char*)(d["membershipType"]|"")); out.ok=out.primary.usedPercent>=0||out.secondary.usedPercent>=0;
  out.status=out.ok?"online (unofficial)":"usage fields missing";
  if (url.startsWith("https://cursor.com/api/")) fetchRecentUsage(request, tls, out.recent30m);
  else out.recent30m.status = "custom endpoint";
  return out;
}

