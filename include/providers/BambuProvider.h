#pragma once
#include <Arduino.h>
#include "AppConfig.h"

struct BambuStatus {
  bool ok = false;
  bool connected = false;
  bool active = false;
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

 private:
  void connect();
  void disconnect();
  void requestPushAll();
  void handleMessage(const char *topic, const uint8_t *payload, unsigned int length);
  static void mqttCallback(char *topic, byte *payload, unsigned int length);

  BambuConfig config;
  BambuStatus latest;
  bool wantActive = false;
  bool configured = false;
  uint32_t lastConnectAttemptMs = 0;
  uint32_t lastPushAllMs = 0;
  String clientId;
};
