#pragma once
#include <Arduino.h>
#include "AppConfig.h"

struct UsageWindow { String label; float usedPercent = -1; String resetText; };
struct UsageSnapshot {
  String provider; bool ok = false; String status; String plan; String updated;
  UsageWindow primary; UsageWindow secondary; UsageWindow tertiary; float credits = -1;
};

class UsageProvider {
 public:
  virtual ~UsageProvider() = default;
  virtual const char *name() const = 0;
  virtual UsageSnapshot fetch(const ProviderConfig &cfg, bool verifyTls) = 0;
};

