#pragma once
#include "AppConfig.h"
#include "HaSensors.h"
#include "providers/UsageProvider.h"

void haMqttBegin(AppConfig &config);
void haMqttConfigure(AppConfig &config);
void haMqttLoop();
void haMqttPublish(const UsageSnapshot &cursor, const UsageSnapshot &codex, const HaDeviceMeta &device);
bool haMqttConnected();
