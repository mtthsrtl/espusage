#include "HaMqtt.h"
#include "BuildInfo.h"
#include <PubSubClient.h>
#include <WiFi.h>
#include <ArduinoJson.h>

struct HaSensorMeta {
  const char *key;
  const char *name;
  const char *unit;
  const char *deviceClass;
  const char *stateClass;
  bool binary;
};

static const HaSensorMeta SENSOR_META[] = {
  {"ready", "Ready", nullptr, nullptr, nullptr, true},
  {"ip", "IP Address", nullptr, nullptr, nullptr, false},
  {"hostname", "Hostname", nullptr, nullptr, nullptr, false},
  {"uptime_ms", "Uptime", "ms", nullptr, "measurement", false},
  {"free_heap", "Free Heap", "B", nullptr, "measurement", false},
  {"rssi", "WiFi RSSI", "dBm", "signal_strength", "measurement", false},
  {"display_mode", "Display Mode", nullptr, nullptr, nullptr, false},
  {"display_on", "Display On", nullptr, nullptr, nullptr, true},
  {"refresh_minutes", "Refresh Interval", "min", nullptr, "measurement", false},
  {"usage_age_ms", "Usage Age", "ms", nullptr, "measurement", false},
  {"version", "Firmware Version", nullptr, nullptr, nullptr, false},

  {"cursor_ok", "Cursor OK", nullptr, nullptr, nullptr, true},
  {"cursor_status", "Cursor Status", nullptr, nullptr, nullptr, false},
  {"cursor_plan", "Cursor Plan", nullptr, nullptr, nullptr, false},
  {"cursor_updated", "Cursor Updated", nullptr, nullptr, nullptr, false},
  {"cursor_models_used_percent", "Cursor Models Used", "%", nullptr, "measurement", false},
  {"cursor_models_elapsed_percent", "Cursor Models Elapsed", "%", nullptr, "measurement", false},
  {"cursor_models_reset", "Cursor Models Reset", nullptr, nullptr, nullptr, false},
  {"cursor_other_used_percent", "Cursor Other Used", "%", nullptr, "measurement", false},
  {"cursor_other_elapsed_percent", "Cursor Other Elapsed", "%", nullptr, "measurement", false},
  {"cursor_other_reset", "Cursor Other Reset", nullptr, nullptr, nullptr, false},
  {"cursor_ondemand_used_percent", "Cursor On Demand Used", "%", nullptr, "measurement", false},
  {"cursor_ondemand_elapsed_percent", "Cursor On Demand Elapsed", "%", nullptr, "measurement", false},
  {"cursor_ondemand_used_amount", "Cursor On Demand Used Amount", nullptr, nullptr, "measurement", false},
  {"cursor_ondemand_limit_amount", "Cursor On Demand Limit", nullptr, nullptr, "measurement", false},
  {"cursor_ondemand_currency", "Cursor On Demand Currency", nullptr, nullptr, nullptr, false},
  {"cursor_ondemand_reset", "Cursor On Demand Reset", nullptr, nullptr, nullptr, false},
  {"cursor_30m_available", "Cursor 30m Available", nullptr, nullptr, nullptr, true},
  {"cursor_30m_partial", "Cursor 30m Partial", nullptr, nullptr, nullptr, true},
  {"cursor_30m_calls", "Cursor 30m Calls", nullptr, nullptr, "measurement", false},
  {"cursor_30m_tokenized_calls", "Cursor 30m Tokenized Calls", nullptr, nullptr, "measurement", false},
  {"cursor_30m_max_mode_calls", "Cursor 30m Max Mode Calls", nullptr, nullptr, "measurement", false},
  {"cursor_30m_tokens", "Cursor 30m Tokens", nullptr, nullptr, "measurement", false},
  {"cursor_30m_input_tokens", "Cursor 30m Input Tokens", nullptr, nullptr, "measurement", false},
  {"cursor_30m_output_tokens", "Cursor 30m Output Tokens", nullptr, nullptr, "measurement", false},
  {"cursor_30m_cache_read_tokens", "Cursor 30m Cache Read Tokens", nullptr, nullptr, "measurement", false},
  {"cursor_30m_cache_write_tokens", "Cursor 30m Cache Write Tokens", nullptr, nullptr, "measurement", false},
  {"cursor_30m_cost_cents", "Cursor 30m Cost", "¢", nullptr, "measurement", false},
  {"cursor_30m_top_model", "Cursor 30m Top Model", nullptr, nullptr, nullptr, false},
  {"cursor_30m_top_kind", "Cursor 30m Top Kind", nullptr, nullptr, nullptr, false},
  {"cursor_30m_status", "Cursor 30m Status", nullptr, nullptr, nullptr, false},

  {"codex_ok", "Codex OK", nullptr, nullptr, nullptr, true},
  {"codex_status", "Codex Status", nullptr, nullptr, nullptr, false},
  {"codex_plan", "Codex Plan", nullptr, nullptr, nullptr, false},
  {"codex_updated", "Codex Updated", nullptr, nullptr, nullptr, false},
  {"codex_credits", "Codex Credits", nullptr, nullptr, "measurement", false},
  {"codex_5h_used_percent", "Codex 5h Used", "%", nullptr, "measurement", false},
  {"codex_5h_elapsed_percent", "Codex 5h Elapsed", "%", nullptr, "measurement", false},
  {"codex_5h_reset", "Codex 5h Reset", nullptr, nullptr, nullptr, false},
  {"codex_weekly_used_percent", "Codex Weekly Used", "%", nullptr, "measurement", false},
  {"codex_weekly_elapsed_percent", "Codex Weekly Elapsed", "%", nullptr, "measurement", false},
  {"codex_weekly_reset", "Codex Weekly Reset", nullptr, nullptr, nullptr, false},
  {"codex_30m_available", "Codex 30m Available", nullptr, nullptr, nullptr, true},
  {"codex_30m_ready", "Codex 30m Ready", nullptr, nullptr, nullptr, true},
  {"codex_30m_samples", "Codex 30m Samples", nullptr, nullptr, "measurement", false},
  {"codex_30m_delta_pp", "Codex 30m Delta", "pp", nullptr, "measurement", false},
  {"codex_30m_status", "Codex 30m Status", nullptr, nullptr, nullptr, false},
};

static AppConfig *cfg = nullptr;
static WiFiClient wifiClient;
static PubSubClient mqtt(wifiClient);
static bool discoveryDone = false;
static uint32_t lastDiscoveryMs = 0;
static uint32_t lastConnectAttemptMs = 0;
static uint32_t connectBackoffMs = 2000;

static String nodeId() {
  String host = cfg && cfg->hostname.length() ? cfg->hostname : "espusage";
  host.replace(" ", "_");
  return host;
}

static String topicPrefix() {
  String prefix = cfg && cfg->mqtt.prefix.length() ? cfg->mqtt.prefix : "espusage";
  while (prefix.endsWith("/")) prefix.remove(prefix.length() - 1);
  return prefix + "/" + nodeId();
}

static String availabilityTopic() { return topicPrefix() + "/status"; }

static String stateTopic(const char *key) { return topicPrefix() + "/" + key; }

static bool ensureConnected() {
  if (!cfg || !cfg->mqtt.enabled || !cfg->mqtt.host.length()) return false;
  if (WiFi.status() != WL_CONNECTED) return false;
  if (mqtt.connected()) return true;
  uint32_t now = millis();
  if (lastConnectAttemptMs && (int32_t)(now - lastConnectAttemptMs) < (int32_t)connectBackoffMs) return false;
  lastConnectAttemptMs = now;
  mqtt.setServer(cfg->mqtt.host.c_str(), cfg->mqtt.port ? cfg->mqtt.port : 1883);
  String clientId = "espusage-" + nodeId() + "-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  String willTopic = availabilityTopic();
  bool ok;
  if (cfg->mqtt.user.length())
    ok = mqtt.connect(clientId.c_str(), cfg->mqtt.user.c_str(), cfg->mqtt.password.c_str(),
                      willTopic.c_str(), 0, true, "offline");
  else
    ok = mqtt.connect(clientId.c_str(), willTopic.c_str(), 0, true, "offline");
  if (!ok) {
    connectBackoffMs = min(connectBackoffMs * 2, (uint32_t)60000);
    Serial.printf("[mqtt][ha] Connect failed to %s:%u, backoff=%lu ms\n",
                  cfg->mqtt.host.c_str(), cfg->mqtt.port, (unsigned long)connectBackoffMs);
    discoveryDone = false;
    return false;
  }
  connectBackoffMs = 2000;
  mqtt.publish(willTopic.c_str(), "online", true);
  discoveryDone = false;
  Serial.printf("[mqtt][ha] Connected to %s:%u as %s\n", cfg->mqtt.host.c_str(), cfg->mqtt.port, clientId.c_str());
  return true;
}

static bool publishDiscovery(const HaSensorMeta &meta) {
  String component = meta.binary ? "binary_sensor" : "sensor";
  String configTopic = "homeassistant/" + component + "/" + nodeId() + "/" + meta.key + "/config";
  JsonDocument doc;
  doc["name"] = meta.name;
  doc["unique_id"] = nodeId() + "_" + meta.key;
  doc["state_topic"] = stateTopic(meta.key);
  doc["availability_topic"] = availabilityTopic();
  doc["payload_available"] = "online";
  doc["payload_not_available"] = "offline";
  if (meta.binary) {
    doc["payload_on"] = "true";
    doc["payload_off"] = "false";
  } else {
    if (meta.unit) doc["unit_of_measurement"] = meta.unit;
    if (meta.deviceClass) doc["device_class"] = meta.deviceClass;
    if (meta.stateClass) doc["state_class"] = meta.stateClass;
  }
  JsonObject device = doc["device"].to<JsonObject>();
  device["identifiers"][0] = "espusage_" + nodeId();
  device["name"] = "ESP Usage (" + nodeId() + ")";
  device["model"] = "GUITION 4848S040";
  device["manufacturer"] = "ESP Usage";
  device["sw_version"] = BUILD_VERSION;
  String payload;
  serializeJson(doc, payload);
  bool ok = mqtt.publish(configTopic.c_str(), payload.c_str(), true);
  if (!ok) Serial.printf("[mqtt][ha] Discovery publish failed for %s (%u bytes)\n", meta.key, payload.length());
  return ok;
}

static void publishDiscoveryAll() {
  if (!cfg || !cfg->mqtt.discovery) { discoveryDone = true; return; }
  for (const HaSensorMeta &meta : SENSOR_META) {
    if (!publishDiscovery(meta)) return;
    delay(5);
    mqtt.loop();
  }
  discoveryDone = true;
  lastDiscoveryMs = millis();
  Serial.printf("[mqtt][ha] Discovery published (%u sensors)\n", (unsigned)(sizeof(SENSOR_META) / sizeof(SENSOR_META[0])));
}

static String valueToPayload(JsonVariantConst value) {
  if (value.isNull()) return "";
  if (value.is<bool>()) return value.as<bool>() ? "true" : "false";
  if (value.is<const char *>()) return String(value.as<const char *>());
  if (value.is<float>() || value.is<double>()) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.4g", value.as<double>());
    return String(buf);
  }
  String out;
  serializeJson(value, out);
  if (out.startsWith("\"") && out.endsWith("\"") && out.length() >= 2) out = out.substring(1, out.length() - 1);
  return out;
}

void haMqttBegin(AppConfig &config) {
  cfg = &config;
  mqtt.setBufferSize(2048);
  mqtt.setKeepAlive(30);
  mqtt.setSocketTimeout(5);
  discoveryDone = false;
  lastConnectAttemptMs = 0;
  connectBackoffMs = 2000;
}

void haMqttConfigure(AppConfig &config) {
  cfg = &config;
  if (mqtt.connected()) mqtt.disconnect();
  discoveryDone = false;
  lastConnectAttemptMs = 0;
  connectBackoffMs = 500;
}

void haMqttLoop() {
  if (!cfg || !cfg->mqtt.enabled || !cfg->mqtt.host.length()) return;
  if (!ensureConnected()) return;
  mqtt.loop();
  if (!discoveryDone || (millis() - lastDiscoveryMs > 1800000UL)) publishDiscoveryAll();
}

void haMqttPublish(const UsageSnapshot &cursor, const UsageSnapshot &codex, const HaDeviceMeta &device) {
  if (!cfg || !cfg->mqtt.enabled || !cfg->mqtt.host.length()) return;
  if (!ensureConnected()) return;
  if (!discoveryDone) publishDiscoveryAll();

  JsonDocument sensors;
  haBuildSensors(sensors, cursor, codex, device);
  JsonObject root = sensors.as<JsonObject>();
  uint16_t published = 0;
  for (JsonPair kv : root) {
    const char *key = kv.key().c_str();
    String payload = valueToPayload(kv.value());
    if (mqtt.publish(stateTopic(key).c_str(), payload.c_str(), true)) published++;
    mqtt.loop();
  }
  mqtt.publish(availabilityTopic().c_str(), "online", true);
  Serial.printf("[mqtt][ha] Published %u state topics\n", published);
}

bool haMqttConnected() { return mqtt.connected(); }
