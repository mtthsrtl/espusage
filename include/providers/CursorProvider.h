#pragma once
#include "providers/UsageProvider.h"
class CursorProvider : public UsageProvider {
 public:
  const char *name() const override { return "Cursor"; }
  UsageSnapshot fetch(const ProviderConfig &cfg, bool verifyTls) override;
  UsageSnapshot fetch(const ProviderConfig &cfg, bool verifyTls, bool includeRecent30m);
};

