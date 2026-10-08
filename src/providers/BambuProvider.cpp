#include "providers/BambuProvider.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <mbedtls/base64.h>

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

static int fanToPercent(int raw) {
  if (raw < 0) return -1;
  if (raw <= 15) return constrain((raw * 100 + 7) / 15, 0, 100);
  return constrain(raw, 0, 100);
}

static int readFanField(JsonVariantConst value) {
  if (value.isNull()) return -1;
  if (value.is<const char *>()) return fanToPercent(String(value.as<const char *>()).toInt());
  return fanToPercent(value.as<int>());
}

static int parseWifiRssi(const String &signal) {
  String digits;
  digits.reserve(signal.length());
  for (size_t i = 0; i < signal.length(); i++) {
    char c = signal[i];
    if (c == '-' || (c >= '0' && c <= '9')) digits += c;
  }
  if (!digits.length()) return 0;
  return digits.toInt();
}

static const char *wifiQualityLabel(int rssi) {
  if (rssi >= -50) return "EXCELLENT";
  if (rssi >= -60) return "GOOD";
  if (rssi >= -70) return "FAIR";
  if (rssi >= -80) return "WEAK";
  return "POOR";
}

static const char *stageLabel(int stageId) {
  switch (stageId) {
    case 0: return "PRINTING";
    case 1: return "BED LEVELING";
    case 2: return "HEATING BED";
    case 3: return "VIBRATION CAL";
    case 4: return "FILAMENT CHANGE";
    case 5: return "M400 PAUSE";
    case 6: return "FILAMENT RUNOUT";
    case 7: return "HEATING NOZZLE";
    case 8: return "EXTRUSION CAL";
    case 9: return "SCANNING BED";
    case 10: return "FIRST LAYER CHECK";
    case 11: return "DETECT PLATE";
    case 12: return "LIDAR CAL";
    case 13: return "HOMING";
    case 14: return "CLEANING NOZZLE";
    case 15: return "CHECK NOZZLE TEMP";
    case 16: return "PAUSED BY USER";
    case 17: return "COVER OPEN";
    case 18: return "LIDAR CAL";
    case 19: return "FLOW CAL";
    case 20: return "NOZZLE TEMP FAULT";
    case 21: return "BED TEMP FAULT";
    default: return "";
  }
}

static String jsonStringField(const String &json, const char *key) {
  String needle = String("\"") + key + "\":\"";
  int start = json.indexOf(needle);
  if (start < 0) return "";
  start += needle.length();
  int end = start;
  while (end < (int)json.length()) {
    char c = json[end];
    if (c == '\\' && end + 1 < (int)json.length()) { end += 2; continue; }
    if (c == '"') break;
    end++;
  }
  if (end >= (int)json.length()) return "";
  return json.substring(start, end);
}

static String uidFromJwt(const String &jwt) {
  int d1 = jwt.indexOf('.');
  int d2 = jwt.indexOf('.', d1 + 1);
  if (d1 < 0 || d2 < 0) return "";
  String payload = jwt.substring(d1 + 1, d2);
  while (payload.length() % 4) payload += '=';
  payload.replace('-', '+');
  payload.replace('_', '/');
  size_t decodedLen = 0;
  unsigned char *decoded = (unsigned char *)malloc(payload.length());
  if (!decoded) return "";
  if (mbedtls_base64_decode(decoded, payload.length(), &decodedLen,
                            (const unsigned char *)payload.c_str(), payload.length()) != 0) {
    free(decoded);
    return "";
  }
  String json((char *)decoded, decodedLen);
  free(decoded);
  String username = jsonStringField(json, "username");
  if (username.startsWith("u_")) return username.substring(2);
  String userId = jsonStringField(json, "user_id");
  if (userId.length()) return userId;
  return "";
}

static void addBambuHeaders(HTTPClient &http) {
  http.addHeader("Content-Type", "application/json");
  http.addHeader("accept", "application/json");
  http.addHeader("User-Agent", "bambu_network_agent/01.09.05.01");
  http.addHeader("X-BBL-Client-Name", "OrcaSlicer");
  http.addHeader("X-BBL-Client-Type", "slicer");
  http.addHeader("X-BBL-Client-Version", "01.09.05.51");
  http.addHeader("X-BBL-Language", "en-US");
  http.addHeader("X-BBL-OS-Type", "linux");
  http.addHeader("X-BBL-Agent-Version", "01.09.05.01");
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
  lastReportMs = 0;
  latest.refreshHz = -1;
  latest.refreshIntervalMs = 0;
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
  http.setTimeout(20000);
  addBambuHeaders(http);

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
  Serial.printf("[bambu][cloud] login HTTP %d, bytes=%u\n", code, (unsigned)response.length());
  if (code <= 0) {
    latest.status = "cloud login network error";
    return false;
  }
  if (!response.length()) {
    latest.status = "cloud login empty response";
    return false;
  }
  if (code == 403 || response.indexOf('<') == 0 ||
      (response.indexOf("cloudflare") >= 0 && response.indexOf("accessToken") < 0)) {
    latest.status = "cloudflare blocked ESP login - run tools/bambu-cloud-login.ps1";
    return false;
  }

  String loginType = jsonStringField(response, "loginType");
  String accessToken = jsonStringField(response, "accessToken");
  String error = jsonStringField(response, "error");
  if (!error.length()) {
    // error may be unquoted null; also try message
    error = jsonStringField(response, "message");
  }

  if (!accessToken.length() && loginType == "verifyCode") {
    latest.needsVerifyCode = true;
    latest.status = "email verification code required";
    // Ask Bambu to send the email code.
    WiFiClientSecure https2;
    https2.setInsecure();
    HTTPClient http2;
    String codeUrl = cloudApiBase() + "/v1/user-service/user/sendemail/code";
    if (http2.begin(https2, codeUrl)) {
      addBambuHeaders(http2);
      JsonDocument codeBody;
      codeBody["email"] = config.account;
      codeBody["type"] = "codeLogin";
      String codePayload;
      serializeJson(codeBody, codePayload);
      int codeHttp = http2.POST(codePayload);
      Serial.printf("[bambu][cloud] sendemail/code HTTP %d\n", codeHttp);
      http2.end();
    }
    return false;
  }
  if (!accessToken.length() && loginType == "tfa") {
    latest.needsVerifyCode = true;
    latest.status = "2FA code required";
    return false;
  }
  if (!accessToken.length()) {
    if (error.length()) latest.status = "cloud login: " + error;
    else latest.status = "cloud login failed HTTP " + String(code);
    return false;
  }

  if (config.cloudToken != accessToken) {
    config.cloudToken = accessToken;
    authDirty = true;
    latest.authDirty = true;
  }
  String uid = uidFromJwt(accessToken);
  if (uid.length() && config.userId != uid) {
    config.userId = uid;
    authDirty = true;
    latest.authDirty = true;
    Serial.printf("[bambu][cloud] uid from jwt=%s\n", uid.c_str());
  }
  if (config.verifyCode.length()) {
    config.verifyCode = "";
    authDirty = true;
    latest.authDirty = true;
  }
  Serial.printf("[bambu][cloud] access token received (%u chars)\n", (unsigned)accessToken.length());
  return true;
}

bool BambuClient::cloudFetchUserId() {
  if (config.userId.length()) return true;
  if (!config.cloudToken.length()) return false;
  String uid = uidFromJwt(config.cloudToken);
  if (uid.length()) {
    config.userId = uid;
    authDirty = true;
    latest.authDirty = true;
    Serial.printf("[bambu][cloud] uid from jwt=%s\n", uid.c_str());
    return true;
  }
  latest.status = "fetching cloud user id...";
  WiFiClientSecure https;
  https.setInsecure();
  HTTPClient http;
  String url = cloudApiBase() + "/v1/design-user-service/my/preference";
  if (!http.begin(https, url)) return false;
  http.setTimeout(15000);
  addBambuHeaders(http);
  http.addHeader("Authorization", "Bearer " + config.cloudToken);
  int code = http.GET();
  String response = http.getString();
  http.end();
  if (code != 200) {
    latest.status = "user id HTTP " + String(code);
    return false;
  }
  // Prefer lightweight extract; preference JSON is small.
  JsonDocument doc;
  if (deserializeJson(doc, response)) {
    latest.status = "user id JSON error";
    return false;
  }
  if (doc["uid"].isNull()) {
    latest.status = "user id missing";
    return false;
  }
  uid = String((long long)doc["uid"].as<long long>());
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
  addBambuHeaders(http);
  http.addHeader("Authorization", "Bearer " + config.cloudToken);
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
  printFilter["spd_mag"] = true;
  printFilter["stg_cur"] = true;
  printFilter["cooling_fan_speed"] = true;
  printFilter["big_fan1_speed"] = true;
  printFilter["wifi_signal"] = true;
  printFilter["gcode_file"] = true;
  printFilter["subtask_name"] = true;
  printFilter["command"] = true;

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
  if (!print["spd_mag"].isNull()) latest.speedPercent = print["spd_mag"].as<int>();
  if (!print["stg_cur"].isNull()) {
    latest.stageId = print["stg_cur"].as<int>();
    latest.stage = stageLabel(latest.stageId);
  }
  int partFan = readFanField(print["cooling_fan_speed"]);
  if (partFan >= 0) latest.partFanPercent = partFan;
  int auxFan = readFanField(print["big_fan1_speed"]);
  if (auxFan >= 0) latest.auxFanPercent = auxFan;

  if (print["wifi_signal"].is<const char *>()) {
    latest.wifiSignal = print["wifi_signal"].as<const char *>();
    latest.wifiRssi = parseWifiRssi(latest.wifiSignal);
    latest.wifiSignalValid = latest.wifiSignal.length() > 0;
    if (latest.wifiSignalValid) latest.linkQuality = wifiQualityLabel(latest.wifiRssi);
  }

  uint32_t now = millis();
  if (lastReportMs > 0) {
    uint32_t gap = now - lastReportMs;
    if (gap < 1) gap = 1;
    if (latest.refreshIntervalMs == 0) latest.refreshIntervalMs = gap;
    else latest.refreshIntervalMs = (latest.refreshIntervalMs * 3 + gap) / 4;
    latest.refreshHz = 1000.0f / (float)latest.refreshIntervalMs;
  }
  lastReportMs = now;
  latest.lastMessageMs = now;

  if (print["subtask_name"].is<const char *>()) latest.fileName = basenameOf(print["subtask_name"].as<const char *>());
  else if (print["gcode_file"].is<const char *>()) latest.fileName = basenameOf(print["gcode_file"].as<const char *>());

  latest.ok = latest.state.length() > 0 || latest.percent >= 0;
  if (latest.state.length()) {
    latest.status = latest.state;
    if (latest.stage.length() && (latest.state == "RUNNING" || latest.state == "PAUSE" || latest.state == "PREPARE"))
      latest.status += " / " + latest.stage;
    if (latest.percent >= 0) latest.status += " " + String(latest.percent) + "%";
    if (latest.remainingMinutes >= 0) latest.status += " - " + formatRemaining(latest.remainingMinutes);
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
