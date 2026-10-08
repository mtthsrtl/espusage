#pragma once
#include <Arduino.h>
#include "AppConfig.h"

struct BambuStatus {
  bool ok = false;
  bool connected = false;
  bool active = false;
  bool authDirty = false;
  bool needsVerifyCode = false;
  String status = "idle";
  String state;
  int percent = -1;
  int remainingMinutes = -1;
  float nozzleTemp = -1;
  float bedTemp = -1;
  float nozzleTarget = -1;
  float bedTarget = -1;
  int layer = -1;
  int totalLayers = -1;
  int speedPercent = -1;
  int stageId = -1;
  int partFanPercent = -1;
  int auxFanPercent = -1;
  String stage;
  String fileName;
  String filament;
  String mode;
};

class BambuClient {
 public:
  void configure(const BambuConfig &cfg);
  void setActive(bool active);
  void loop();
  BambuStatus snapshot() const;
  bool isActive() const { return wantActive; }
  bool takeAuthDirty();
  const BambuConfig &currentConfig() const { return config; }

 private:
  void connect();
  void disconnect();
  void requestPushAll();
  bool ensureCloudAuth(bool forceLogin);
  bool cloudLogin();
  bool cloudFetchUserId();
  bool cloudFillSerialIfNeeded();
  String cloudApiBase() const;
  void handleMessage(const char *topic, const uint8_t *payload, unsigned int length);
  static void mqttCallback(char *topic, byte *payload, unsigned int length);

  BambuConfig config;
  BambuStatus latest;
  bool wantActive = false;
  bool configured = false;
  bool authDirty = false;
  uint32_t lastConnectAttemptMs = 0;
  uint32_t lastPushAllMs = 0;
  String clientId;
};
