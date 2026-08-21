#pragma once
#include "providers/UsageProvider.h"
void displayBegin();
void displayLoop();
void displaySetBrightness(uint8_t value);
void displayUpdate(const UsageSnapshot &codex, const UsageSnapshot &cursor, uint8_t warningPercent, uint8_t criticalPercent);
void displaySetNetwork(const String &text, bool connected);

