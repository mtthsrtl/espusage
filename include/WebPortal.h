#pragma once
#include "AppConfig.h"
#include "providers/UsageProvider.h"
using UsageRefreshHandler = void (*)();
void webBegin(AppConfig &config, bool setupMode, UsageRefreshHandler refreshHandler);
void webLoop();
void webUpdateUsage(const UsageSnapshot &codex, const UsageSnapshot &cursor);

