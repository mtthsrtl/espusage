#pragma once
#include <Arduino.h>
#include "providers/UsageProvider.h"

struct TouchDiagnostics {
  String status;
  String controller;
  String busDevices;
  String lastError;
  String lastEvent;
  uint8_t address;
  uint8_t lastState;
  uint8_t lastTouchCount;
  uint16_t rawWidth;
  uint16_t rawHeight;
  uint16_t rawX;
  uint16_t rawY;
  int16_t displayX;
  int16_t displayY;
  uint32_t callbackCalls;
  uint32_t fallbackReads;
  uint32_t probeAttempts;
  uint32_t polls;
  uint32_t stateReads;
  uint32_t stateReadErrors;
  uint32_t readyFrames;
  uint32_t pointFrames;
  uint32_t acknowledgeErrors;
  uint32_t downEvents;
  uint32_t upEvents;
  uint32_t tapEvents;
  uint32_t toggleEvents;
  uint32_t lastEventMs;
  bool pressed;
  bool possibleGsl3680;
  bool remainingView;
};

void displayBegin(const AppConfig &config);
void displayLoop();
void displaySetBrightness(uint8_t value);
bool displayConsumeTouchActivity();
bool displaySetRemainingView(bool remaining);
bool displayToggleRemainingView();
void displayUpdate(const UsageSnapshot &codex, const UsageSnapshot &cursor, uint8_t warningPercent, uint8_t criticalPercent);
void displaySetNetwork(const String &text, bool connected);
TouchDiagnostics displayGetTouchDiagnostics();
const uint16_t *displayGetFramebuffer();

