#pragma once
#include <Arduino.h>

struct ProviderConfig {
  bool enabled = false;
  String endpoint;
  String token;
  String session;
};

struct AppConfig {
  String wifiSsid;
  String wifiPassword;
  String hostname = "espusage";
  ProviderConfig codex;
  ProviderConfig cursor;
  uint8_t brightness = 85;
  uint16_t refreshMinutes = 5;
  bool verifyTls = true;
};

bool loadConfig(AppConfig &cfg);
bool saveConfig(const AppConfig &cfg);
void eraseConfig();

