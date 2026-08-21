#pragma once
#include "providers/UsageProvider.h"
class CodexProvider : public UsageProvider { public: const char *name() const override { return "Codex"; } UsageSnapshot fetch(const ProviderConfig&, bool) override; };

