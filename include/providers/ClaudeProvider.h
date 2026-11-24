#pragma once
#include "providers/UsageProvider.h"

class ClaudeProvider : public UsageProvider {
 public:
  const char *name() const override { return "Claude"; }
  UsageSnapshot fetch(const ProviderConfig &cfg, bool verifyTls) override;
  static void seedPlanCache(const String &plan);
};
