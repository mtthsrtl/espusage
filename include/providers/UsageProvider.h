#pragma once
#include <Arduino.h>
#include "AppConfig.h"

struct UsageWindow {
  String label;
  float usedPercent = -1;
  float elapsedPercent = -1;
  bool monetary = false;
  float usedAmount = -1;
  float limitAmount = -1;
  String currencySymbol;
  String resetText;
};

struct RecentUsageBucket {
  uint64_t tokens = 0;
  uint16_t calls = 0;
  float deltaPercent = 0;
  bool valid = false;
};

struct RecentUsage30m {
  bool available = false;
  bool ready = false;
  bool partial = false;
  bool tokenData = false;
  uint16_t calls = 0;
  uint16_t tokenizedCalls = 0;
  uint16_t maxModeCalls = 0;
  uint16_t samples = 0;
  uint64_t inputTokens = 0;
  uint64_t outputTokens = 0;
  uint64_t cacheWriteTokens = 0;
  uint64_t cacheReadTokens = 0;
  float costCents = -1;
  float deltaPercent = 0;
  String topModel;
  String topKind;
  String status;
  RecentUsageBucket buckets[6];
};

struct UsageSnapshot {
  String provider; bool ok = false; String status; String plan; String updated;
  UsageWindow primary; UsageWindow secondary; UsageWindow tertiary; float credits = -1;
  RecentUsage30m recent30m;
};

class UsageProvider {
 public:
  virtual ~UsageProvider() = default;
  virtual const char *name() const = 0;
  virtual UsageSnapshot fetch(const ProviderConfig &cfg, bool verifyTls) = 0;
};

