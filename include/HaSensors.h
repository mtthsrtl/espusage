#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "providers/UsageProvider.h"

struct HaDeviceMeta {
  bool ready = false;
  String ip;
  String hostname = "espusage";
  uint32_t uptimeMs = 0;
  uint32_t freeHeap = 0;
  int32_t rssi = 0;
  String displayMode = "used";
  bool displayOn = true;
  uint16_t refreshMinutes = 5;
  uint32_t usageAgeMs = 0;
  String version;
};

// Fills a flat JSON object with all HA sensor keys. Returns sensor count.
size_t haBuildSensors(JsonDocument &out, const UsageSnapshot &cursor, const UsageSnapshot &codex,
                      const HaDeviceMeta &device);

// Copies only keys with the given prefix (e.g. "cursor_", "codex_") plus optional extras.
void haFilterPrefix(const JsonDocument &all, JsonDocument &out, const char *prefix);

void haFilterDevice(const JsonDocument &all, JsonDocument &out);
