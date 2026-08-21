#pragma once
#include "AppConfig.h"
#include "providers/UsageProvider.h"
void webBegin(AppConfig &config, bool setupMode);
void webLoop();
void webUpdateUsage(const UsageSnapshot &codex, const UsageSnapshot &cursor);

