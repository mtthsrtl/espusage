#pragma once
#include <Arduino.h>

struct ProviderConfig {
  bool enabled = false;
  String endpoint;
  String token;
  String session;
  String accountId;
};

struct AppConfig {
  String wifiSsid;
  String wifiPassword;
  bool wifiProvisioned = false;
  String hostname = "espusage";
  ProviderConfig codex;
  ProviderConfig cursor;
  uint8_t brightness = 85;
  uint16_t refreshMinutes = 5;
  uint8_t warningPercent = 70;
  uint8_t criticalPercent = 90;
  bool verifyTls = true;
  uint8_t displayStyle = 0;  // 0 = framed panels, 1 = open/frameless
  bool showCursorModels = true;
  bool showCursorOther = true;
  bool showCursorOnDemand = true;
  bool showCursorThirtyMinute = true;
  bool showCodexWeekly = true;
};

bool loadConfig(AppConfig &cfg);
bool saveConfig(const AppConfig &cfg);
bool saveWifiConfig(AppConfig &cfg, const String &ssid, const String &password);
bool eraseWifiConfig(AppConfig &cfg);
void eraseConfig();

