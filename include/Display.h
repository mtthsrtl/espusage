#pragma once
#include "providers/UsageProvider.h"
void displayBegin();
void displayLoop();
void displaySetBrightness(uint8_t value);
void displayUpdate(const UsageSnapshot &codex, const UsageSnapshot &cursor);
void displaySetNetwork(const String &text, bool connected);

