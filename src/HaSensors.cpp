#include "HaSensors.h"

static void putWindow(JsonObject out, const char *prefix, const UsageWindow &window) {
  String usedKey = String(prefix) + "_used_percent";
  String elapsedKey = String(prefix) + "_elapsed_percent";
  String resetKey = String(prefix) + "_reset";
  if (window.usedPercent >= 0) out[usedKey] = window.usedPercent;
  else out[usedKey] = nullptr;
  if (window.elapsedPercent >= 0) out[elapsedKey] = window.elapsedPercent;
  else out[elapsedKey] = nullptr;
  out[resetKey] = window.resetText;
}

static void putOnDemand(JsonObject out, const UsageWindow &window) {
  putWindow(out, "cursor_ondemand", window);
  if (window.usedAmount >= 0) out["cursor_ondemand_used_amount"] = window.usedAmount;
  else out["cursor_ondemand_used_amount"] = nullptr;
  if (window.limitAmount >= 0) out["cursor_ondemand_limit_amount"] = window.limitAmount;
  else out["cursor_ondemand_limit_amount"] = nullptr;
  out["cursor_ondemand_currency"] = window.currencySymbol;
}

size_t haBuildSensors(JsonDocument &out, const UsageSnapshot &cursor, const UsageSnapshot &codex,
                      const HaDeviceMeta &device) {
  out.clear();
  JsonObject root = out.to<JsonObject>();

  root["ready"] = device.ready;
  root["ip"] = device.ip;
  root["hostname"] = device.hostname;
  root["uptime_ms"] = device.uptimeMs;
  root["free_heap"] = device.freeHeap;
  root["rssi"] = device.rssi;
  root["display_mode"] = device.displayMode;
  root["display_on"] = device.displayOn;
  root["refresh_minutes"] = device.refreshMinutes;
  root["usage_age_ms"] = device.usageAgeMs;
  root["version"] = device.version;

  root["cursor_ok"] = cursor.ok;
  root["cursor_status"] = cursor.status;
  root["cursor_plan"] = cursor.plan;
  root["cursor_updated"] = cursor.updated;
  putWindow(root, "cursor_models", cursor.primary);
  putWindow(root, "cursor_other", cursor.secondary);
  putOnDemand(root, cursor.tertiary);
  const RecentUsage30m &cr = cursor.recent30m;
  root["cursor_30m_available"] = cr.available;
  root["cursor_30m_partial"] = cr.partial;
  root["cursor_30m_calls"] = cr.calls;
  root["cursor_30m_tokenized_calls"] = cr.tokenizedCalls;
  root["cursor_30m_max_mode_calls"] = cr.maxModeCalls;
  root["cursor_30m_tokens"] = cr.inputTokens + cr.outputTokens + cr.cacheReadTokens + cr.cacheWriteTokens;
  root["cursor_30m_input_tokens"] = cr.inputTokens;
  root["cursor_30m_output_tokens"] = cr.outputTokens;
  root["cursor_30m_cache_read_tokens"] = cr.cacheReadTokens;
  root["cursor_30m_cache_write_tokens"] = cr.cacheWriteTokens;
  if (cr.costCents >= 0) root["cursor_30m_cost_cents"] = cr.costCents;
  else root["cursor_30m_cost_cents"] = nullptr;
  root["cursor_30m_top_model"] = cr.topModel;
  root["cursor_30m_top_kind"] = cr.topKind;
  root["cursor_30m_status"] = cr.status;

  root["codex_ok"] = codex.ok;
  root["codex_status"] = codex.status;
  root["codex_plan"] = codex.plan;
  root["codex_updated"] = codex.updated;
  if (codex.credits >= 0) root["codex_credits"] = codex.credits;
  else root["codex_credits"] = nullptr;
  putWindow(root, "codex_5h", codex.primary);
  putWindow(root, "codex_weekly", codex.secondary);
  const RecentUsage30m &xr = codex.recent30m;
  root["codex_30m_available"] = xr.available;
  root["codex_30m_ready"] = xr.ready;
  root["codex_30m_samples"] = xr.samples;
  root["codex_30m_delta_pp"] = xr.deltaPercent;
  root["codex_30m_status"] = xr.status;

  return root.size();
}

void haFilterPrefix(const JsonDocument &all, JsonDocument &out, const char *prefix) {
  out.clear();
  JsonObject dst = out.to<JsonObject>();
  String p = prefix ? prefix : "";
  for (JsonPairConst kv : all.as<JsonObjectConst>()) {
    if (p.length() == 0 || String(kv.key().c_str()).startsWith(p)) dst[kv.key()] = kv.value();
  }
}

void haFilterDevice(const JsonDocument &all, JsonDocument &out) {
  static const char *keys[] = {
      "ready", "ip", "hostname", "uptime_ms", "free_heap", "rssi", "display_mode",
      "display_on", "refresh_minutes", "usage_age_ms", "version"};
  out.clear();
  JsonObject dst = out.to<JsonObject>();
  JsonObjectConst src = all.as<JsonObjectConst>();
  for (const char *key : keys) dst[key] = src[key];
}
