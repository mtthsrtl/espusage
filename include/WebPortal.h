#pragma once
#include "AppConfig.h"
#include "providers/UsageProvider.h"
#include "providers/BambuProvider.h"
using UsageRefreshHandler = void (*)();
using ViewChangeHandler = bool (*)(const char *screen);
using BambuSessionHandler = void (*)();
void webBegin(AppConfig &config, bool setupMode, UsageRefreshHandler refreshHandler, ViewChangeHandler viewHandler = nullptr, BambuSessionHandler sessionHandler = nullptr);
void webLoop();
void webUpdateUsage(const UsageSnapshot &codex, const UsageSnapshot &cursor);
void webUpdatePrint(const BambuStatus &status);
