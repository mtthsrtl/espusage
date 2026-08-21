#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "AppConfig.h"
bool getJson(const String &url, const ProviderConfig &cfg, bool verifyTls, JsonDocument &doc, String &error);
bool postJson(const String &url, const ProviderConfig &cfg, bool verifyTls, const String &body, JsonDocument &doc, String &error);

