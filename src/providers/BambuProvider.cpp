#include "providers/BambuProvider.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
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

String BambuClient::cloudApiBase() const {
  return config.region == 1 ? "https://api.bambulab.cn" : "https://api.bambulab.com";
}

void BambuClient::configure(const BambuConfig &cfg) {
  config = cfg;
  latest.mode = cfg.mode == 1 ? "cloud" : "local";
  latest.needsVerifyCode = false;
  bool localOk = cfg.mode == 0 && cfg.host.length() > 0 && cfg.accessCode.length() > 0 && cfg.serial.length() > 0;
  bool cloudOk = cfg.mode == 1 &&
                 ((cfg.account.length() > 0 && cfg.password.length() > 0) ||
                  (cfg.userId.length() > 0 && cfg.cloudToken.length() > 0));
  configured = cfg.enabled && (localOk || cloudOk);
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

bool BambuClient::takeAuthDirty() {
  bool dirty = authDirty;
  authDirty = false;
  latest.authDirty = false;
  return dirty;
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

bool BambuClient::cloudLogin() {
  if (!config.account.length()) {
    latest.status = "cloud account missing";
    return false;
  }
  latest.status = "cloud login...";
  latest.needsVerifyCode = false;
  WiFiClientSecure https;
  https.setInsecure();
  HTTPClient http;
  String url = cloudApiBase() + "/v1/user-service/user/login";
  if (!http.begin(https, url)) {
    latest.status = "cloud login begin failed";
    return false;
  }
  http.setTimeout(15000);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("User-Agent", "espusage/0.9.8");

  JsonDocument body;
  body["account"] = config.account;
  if (config.verifyCode.length()) {
    body["code"] = config.verifyCode;
  } else {
    body["password"] = config.password;
  }
  body["apiError"] = "";
  String payload;
  serializeJson(body, payload);
  int code = http.POST(payload);
  String response = http.getString();
  http.end();
  Serial.printf("[bambu][cloud] login HTTP %d\n", code);
  if (code <= 0) {
    latest.status = "cloud login network error";
    return false;
  }

  JsonDocument doc;
  if (deserializeJson(doc, response)) {
    latest.status = "cloud login JSON error";
    return false;
  }

  String loginType = doc["loginType"] | "";
  String accessToken = doc["accessToken"] | "";
  if (!accessToken.length() && loginType == "verifyCode") {
    latest.needsVerifyCode = true;
    latest.status = "email verification code required";
    Serial.println("[bambu][cloud] Bambu asks for verifyCode");
    return false;
  }
  if (!accessToken.length() && loginType == "tfa") {
    latest.needsVerifyCode = true;
    latest.status = "2FA code required";
    return false;
  }
  if (!accessToken.length()) {
    String message = doc["message"] | doc["error"] | "login failed";
    latest.status = "cloud login: " + message;
    return false;
  }

  if (config.cloudToken != accessToken) {
    config.cloudToken = accessToken;
    authDirty = true;
    latest.authDirty = true;
  }
  if (config.verifyCode.length()) {
    config.verifyCode = "";
    authDirty = true;
    latest.authDirty = true;
  }
  Serial.println("[bambu][cloud] access token received");
  return true;
}

bool BambuClient::cloudFetchUserId() {
  if (config.userId.length()) return true;
  if (!config.cloudToken.length()) return false;
  latest.status = "fetching cloud user id...";
  WiFiClientSecure https;
  https.setInsecure();
  HTTPClient http;
  String url = cloudApiBase() + "/v1/design-user-service/my/preference";
  if (!http.begin(https, url)) return false;
  http.setTimeout(15000);
  http.addHeader("Authorization", "Bearer " + config.cloudToken);
  http.addHeader("User-Agent", "espusage/0.9.8");
  int code = http.GET();
  String response = http.getString();
  http.end();
  if (code != 200) {
    latest.status = "user id HTTP " + String(code);
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, response)) {
    latest.status = "user id JSON error";
    return false;
  }
  if (doc["uid"].isNull()) {
    latest.status = "user id missing";
    return false;
  }
  String uid = String(doc["uid"].as<long long>());
  if (!uid.length()) return false;
  config.userId = uid;
  authDirty = true;
  latest.authDirty = true;
  Serial.printf("[bambu][cloud] uid=%s\n", uid.c_str());
  return true;
}

bool BambuClient::cloudFillSerialIfNeeded() {
  if (config.serial.length()) return true;
  if (!config.cloudToken.length()) return false;
  latest.status = "fetching printer serial...";
  WiFiClientSecure https;
  https.setInsecure();
  HTTPClient http;
  String url = cloudApiBase() + "/v1/iot-service/api/user/bind";
  if (!http.begin(https, url)) return false;
  http.setTimeout(15000);
  http.addHeader("Authorization", "Bearer " + config.cloudToken);
  http.addHeader("User-Agent", "espusage/0.9.8");
  int code = http.GET();
  String response = http.getString();
  http.end();
  if (code != 200) {
    latest.status = "device list HTTP " + String(code);
    return false;
  }
  JsonDocument doc;
  if (deserializeJson(doc, response)) {
    latest.status = "device list JSON error";
    return false;
  }
  if (!doc["devices"].is<JsonArray>() || doc["devices"].as<JsonArray>().size() == 0) {
    latest.status = "no cloud printers bound";
    return false;
  }
  JsonArray devices = doc["devices"].as<JsonArray>();
  String chosen;
  for (JsonObject device : devices) {
    String id = device["dev_id"] | "";
    String product = device["dev_product_name"] | "";
    String model = device["dev_model_name"] | "";
    if (!id.length()) continue;
    if (product.indexOf("A1") >= 0 || model.indexOf("A1") >= 0 || product.indexOf("N2S") >= 0) {
      chosen = id;
      break;
    }
    if (!chosen.length()) chosen = id;
  }
  if (!chosen.length()) {
    latest.status = "printer serial missing";
    return false;
  }
  config.serial = chosen;
  authDirty = true;
  latest.authDirty = true;
  Serial.printf("[bambu][cloud] serial=%s\n", chosen.c_str());
  return true;
}

bool BambuClient::ensureCloudAuth(bool forceLogin) {
  if (config.mode != 1) return true;
  if (forceLogin || !config.cloudToken.length()) {
    if (!config.password.length() && !config.verifyCode.length()) {
      latest.status = "cloud password missing";
      return false;
    }
    if (!cloudLogin()) return false;
  }
  if (!cloudFetchUserId()) {
    // Token may be stale; retry login once.
    if (!forceLogin && config.password.length() && cloudLogin()) return cloudFetchUserId() && cloudFillSerialIfNeeded();
    return false;
  }
  return cloudFillSerialIfNeeded();
}

void BambuClient::connect() {
  if (!configured || WiFi.status() != WL_CONNECTED) return;
  uint32_t now = millis();
  if (lastConnectAttemptMs && now - lastConnectAttemptMs < 8000) return;
  lastConnectAttemptMs = now;

  if (config.mode == 1 && !ensureCloudAuth(false)) {
    latest.connected = false;
    latest.ok = false;
    return;
  }
  if (!config.serial.length()) {
    latest.status = "printer serial missing";
    latest.connected = false;
    return;
  }

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
    int state = mqttClient.state();
    latest.status = "mqtt connect failed: " + String(state);
    Serial.printf("[bambu][mqtt] Connect failed, state=%d\n", state);
    if (config.mode == 1 && config.password.length()) {
      Serial.println("[bambu][cloud] Retrying login after MQTT failure");
      config.cloudToken = "";
      ensureCloudAuth(true);
    }
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
  latest.needsVerifyCode = false;
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

BambuStatus BambuClient::snapshot() const {
  BambuStatus copy = latest;
  copy.authDirty = authDirty;
  return copy;
}
