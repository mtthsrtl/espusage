#include "providers/BambuProvider.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

static BambuClient *activeClient = nullptr;
static WiFiClientSecure tlsClient;
static PubSubClient mqttClient(tlsClient);

static String basenameOf(const String &path) {
  int slash = path.lastIndexOf('/');
  if (slash < 0) slash = path.lastIndexOf('\\');
  return slash >= 0 ? path.substring(slash + 1) : path;
}

static String formatRemaining(int minutes) {
  if (minutes < 0) return "--";
  if (minutes < 60) return String(minutes) + "m";
  int hours = minutes / 60, mins = minutes % 60;
  if (hours < 48) return String(hours) + "h " + String(mins) + "m";
  return String(hours / 24) + "d " + String(hours % 24) + "h";
}

void BambuClient::mqttCallback(char *topic, byte *payload, unsigned int length) {
  if (activeClient) activeClient->handleMessage(topic, payload, length);
}

void BambuClient::configure(const BambuConfig &cfg) {
  config = cfg;
  configured = cfg.enabled && cfg.serial.length() > 0 &&
               ((cfg.mode == 0 && cfg.host.length() > 0 && cfg.accessCode.length() > 0) ||
                (cfg.mode == 1 && cfg.userId.length() > 0 && cfg.cloudToken.length() > 0));
  latest.mode = cfg.mode == 1 ? "cloud" : "local";
  if (!configured) {
    setActive(false);
    latest.status = cfg.enabled ? "printer config incomplete" : "disabled";
    latest.ok = false;
    latest.connected = false;
  }
}

void BambuClient::setActive(bool active) {
  wantActive = active && configured;
  latest.active = wantActive;
  if (!wantActive) {
    disconnect();
    if (!configured) return;
    latest.status = "disconnected";
    latest.connected = false;
  }
}

void BambuClient::disconnect() {
  if (mqttClient.connected()) {
    Serial.println("[bambu][mqtt] Disconnecting");
    mqttClient.disconnect();
  }
  tlsClient.stop();
  if (activeClient == this) activeClient = nullptr;
  latest.connected = false;
}

void BambuClient::requestPushAll() {
  if (!mqttClient.connected() || !config.serial.length()) return;
  String topic = "device/" + config.serial + "/request";
  const char *payload = "{\"pushing\":{\"sequence_id\":\"0\",\"command\":\"pushall\"}}";
  if (mqttClient.publish(topic.c_str(), payload)) {
    lastPushAllMs = millis();
    Serial.println("[bambu][mqtt] pushall requested");
  }
}

void BambuClient::connect() {
  if (!configured || WiFi.status() != WL_CONNECTED) return;
  uint32_t now = millis();
  if (lastConnectAttemptMs && now - lastConnectAttemptMs < 5000) return;
  lastConnectAttemptMs = now;

  disconnect();
  activeClient = this;
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(24576);
  mqttClient.setKeepAlive(30);
  mqttClient.setSocketTimeout(10);

  const char *host = config.mode == 1
                         ? (config.region == 1 ? "cn.mqtt.bambulab.com" : "us.mqtt.bambulab.com")
                         : config.host.c_str();
  String username = config.mode == 1 ? ("u_" + config.userId) : String("bblp");
  String password = config.mode == 1 ? config.cloudToken : config.accessCode;
  clientId = "espusage-" + String((uint32_t)ESP.getEfuseMac(), HEX) + "-" + String(now & 0xFFFF, HEX);

  tlsClient.setInsecure();
  mqttClient.setServer(host, 8883);
  latest.status = String("connecting ") + host;
  Serial.printf("[bambu][mqtt] Connecting to %s as %s (mode=%s)\n", host, username.c_str(),
                config.mode == 1 ? "cloud" : "local");

  if (!mqttClient.connect(clientId.c_str(), username.c_str(), password.c_str())) {
    latest.connected = false;
    latest.ok = false;
    latest.status = "mqtt connect failed: " + String(mqttClient.state());
    Serial.printf("[bambu][mqtt] Connect failed, state=%d\n", mqttClient.state());
    return;
  }

  String report = "device/" + config.serial + "/report";
  if (!mqttClient.subscribe(report.c_str())) {
    latest.status = "subscribe failed";
    latest.ok = false;
    disconnect();
    return;
  }
  latest.connected = true;
  latest.status = "waiting for status";
  Serial.printf("[bambu][mqtt] Subscribed to %s\n", report.c_str());
  requestPushAll();
}

void BambuClient::handleMessage(const char *topic, const uint8_t *payload, unsigned int length) {
  if (!topic || !payload || !length) return;
  JsonDocument filter;
  JsonObject printFilter = filter["print"].to<JsonObject>();
  printFilter["gcode_state"] = true;
  printFilter["mc_percent"] = true;
  printFilter["mc_remaining_time"] = true;
  printFilter["nozzle_temper"] = true;
  printFilter["bed_temper"] = true;
  printFilter["nozzle_target_temper"] = true;
  printFilter["bed_target_temper"] = true;
  printFilter["layer_num"] = true;
  printFilter["total_layer_num"] = true;
  printFilter["gcode_file"] = true;
  printFilter["subtask_name"] = true;
  printFilter["command"] = true;
  JsonObject amsFilter = printFilter["ams"].to<JsonObject>();
  JsonArray amsUnits = amsFilter["ams"].to<JsonArray>();
  JsonObject amsUnit = amsUnits.add<JsonObject>();
  JsonArray trays = amsUnit["tray"].to<JsonArray>();
  JsonObject tray = trays.add<JsonObject>();
  tray["tray_type"] = true;
  tray["tray_color"] = true;
  tray["tray_sub_brands"] = true;
  JsonObject vt = printFilter["vt_tray"].to<JsonObject>();
  vt["tray_type"] = true;
  vt["tray_color"] = true;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload, length, DeserializationOption::Filter(filter));
  if (err) {
    Serial.printf("[bambu][mqtt] JSON parse: %s (%u bytes)\n", err.c_str(), length);
    return;
  }
  if (!doc["print"].is<JsonObject>()) return;
  JsonObject print = doc["print"].as<JsonObject>();

  if (print["gcode_state"].is<const char *>()) latest.state = print["gcode_state"].as<const char *>();
  if (!print["mc_percent"].isNull()) latest.percent = print["mc_percent"].as<int>();
  if (!print["mc_remaining_time"].isNull()) latest.remainingMinutes = print["mc_remaining_time"].as<int>();
  if (!print["nozzle_temper"].isNull()) latest.nozzleTemp = print["nozzle_temper"].as<float>();
  if (!print["bed_temper"].isNull()) latest.bedTemp = print["bed_temper"].as<float>();
  if (!print["nozzle_target_temper"].isNull()) latest.nozzleTarget = print["nozzle_target_temper"].as<float>();
  if (!print["bed_target_temper"].isNull()) latest.bedTarget = print["bed_target_temper"].as<float>();
  if (!print["layer_num"].isNull()) latest.layer = print["layer_num"].as<int>();
  if (!print["total_layer_num"].isNull()) latest.totalLayers = print["total_layer_num"].as<int>();

  String file;
  if (print["subtask_name"].is<const char *>()) file = print["subtask_name"].as<const char *>();
  if (!file.length() && print["gcode_file"].is<const char *>()) file = print["gcode_file"].as<const char *>();
  if (file.length()) latest.fileName = basenameOf(file);

  String filament;
  if (print["ams"]["ams"].is<JsonArray>()) {
    for (JsonObject unit : print["ams"]["ams"].as<JsonArray>()) {
      if (!unit["tray"].is<JsonArray>()) continue;
      for (JsonObject t : unit["tray"].as<JsonArray>()) {
        String type = t["tray_type"] | "";
        if (!type.length()) continue;
        String color = t["tray_color"] | "";
        String brand = t["tray_sub_brands"] | "";
        if (filament.length()) filament += " | ";
        filament += type;
        if (brand.length()) filament += " " + brand;
        if (color.length()) filament += " #" + color;
      }
    }
  }
  if (!filament.length() && print["vt_tray"].is<JsonObject>()) {
    String type = print["vt_tray"]["tray_type"] | "";
    String color = print["vt_tray"]["tray_color"] | "";
    if (type.length()) {
      filament = type;
      if (color.length()) filament += " #" + color;
      filament += " (ext)";
    }
  }
  if (filament.length()) latest.filament = filament;

  latest.ok = latest.state.length() > 0 || latest.percent >= 0;
  if (latest.state.length()) {
    latest.status = latest.state;
    if (latest.percent >= 0) latest.status += " " + String(latest.percent) + "%";
    if (latest.remainingMinutes >= 0) latest.status += " · " + formatRemaining(latest.remainingMinutes);
  } else {
    latest.status = "online";
  }
  Serial.printf("[bambu][mqtt] %s file=%s layer=%d/%d\n", latest.status.c_str(), latest.fileName.c_str(),
                latest.layer, latest.totalLayers);
}

void BambuClient::loop() {
  if (!wantActive) return;
  if (!mqttClient.connected()) {
    latest.connected = false;
    connect();
    return;
  }
  latest.connected = true;
  mqttClient.loop();
  if (millis() - lastPushAllMs > 60000UL) requestPushAll();
}

BambuStatus BambuClient::snapshot() const { return latest; }
