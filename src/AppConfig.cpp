#include "AppConfig.h"
#include <Preferences.h>

static Preferences prefs;
static constexpr uint8_t UI_CONFIG_VERSION = 4;

bool loadConfig(AppConfig &c) {
  if (!prefs.begin("espusage", true)) return false;
  c.wifiSsid = prefs.getString("ssid", "");
  c.wifiPassword = prefs.getString("wifi_pw", "");
  c.wifiProvisioned = prefs.getBool("wifi_saved", false);
  c.hostname = prefs.getString("host", "espusage");
  c.codex.enabled = prefs.getBool("codex_on", false);
  c.codex.endpoint = prefs.getString("codex_url", "");
  c.codex.token = prefs.getString("codex_tok", "");
  c.codex.session = prefs.getString("codex_sess", "");
  c.codex.accountId = prefs.getString("codex_acct", "");
  c.codexCreditsFallback = prefs.getBool("cdx_credits", false);
  c.cursor.enabled = prefs.getBool("cursor_on", false);
  c.cursor.endpoint = prefs.getString("cursor_url", "https://cursor.com/api/usage-summary");
  c.cursor.token = prefs.getString("cursor_tok", "");
  c.cursor.session = prefs.getString("cursor_sess", "");
  c.brightness = prefs.getUChar("bright", 85);
  c.refreshMinutes = prefs.getUShort("refresh", 5);
  c.warningPercent = prefs.getUChar("warn_pct", 70);
  c.criticalPercent = prefs.getUChar("crit_pct", 90);
  c.overpaceColor = prefs.getUInt("over_color", 0xDDF542) & 0xFFFFFF;
  c.warningColor = prefs.getUInt("warn_color", 0xF0A020) & 0xFFFFFF;
  c.backgroundColor = prefs.getUInt("bg_color", 0x000000) & 0xFFFFFF;
  c.paceIndicatorColor = prefs.getUInt("pace_color", 0xFFFFFF) & 0xFFFFFF;
  c.paceIndicatorGlowColor = prefs.getUInt("pace_glow_color", c.paceIndicatorColor) & 0xFFFFFF;
  c.paceIndicatorGlow = prefs.getBool("pace_glow", false);
  c.verifyTls = prefs.getBool("tls", true);
  c.displayStyle = prefs.getUChar("ui_style", 0);
  if (c.displayStyle > 4) c.displayStyle = 0;
  c.displayAvailable = prefs.getBool("ui_remaining", false);
  c.displayOffEnabled = prefs.getBool("off_on", false);
  c.displayOffFromMinutes = prefs.getUShort("off_from", 22 * 60);
  c.displayOffUntilMinutes = prefs.getUShort("off_until", 7 * 60);
  if (c.displayOffFromMinutes > 1439) c.displayOffFromMinutes = 22 * 60;
  if (c.displayOffUntilMinutes > 1439) c.displayOffUntilMinutes = 7 * 60;
  c.showCursorModels = prefs.getBool("show_cur_main", true);
  c.showCursorOther = prefs.getBool("show_cur_other", true);
  c.showCursorOnDemand = prefs.getBool("show_cur_od", true);
  c.showCursorThirtyMinute = prefs.getBool("show_cur_30m", true);
  uint8_t uiConfigVersion = prefs.getUChar("ui_ver", 0);
  c.showCodexFiveHour = prefs.getBool("show_cdx_5h", true);
  c.showCodexWeekly = prefs.getBool("show_cdx_7d", true);
  c.showCodexThirtyMinute = prefs.getBool("show_cdx_30m", true);
  prefs.end();
  if (uiConfigVersion < UI_CONFIG_VERSION && prefs.begin("espusage", false)) {
    prefs.remove("codex_evt_url");
    prefs.putUChar("ui_ver", UI_CONFIG_VERSION);
    prefs.end();
    Serial.println("[config][nvs] Display settings migrated; Codex 5-hour row enabled");
  }
  return true;
}

bool saveConfig(const AppConfig &c) {
  if (!prefs.begin("espusage", false)) return false;
  prefs.putString("ssid", c.wifiSsid); prefs.putString("wifi_pw", c.wifiPassword);
  prefs.putBool("wifi_saved", c.wifiProvisioned && c.wifiSsid.length() > 0);
  prefs.putString("host", c.hostname);
  prefs.putBool("codex_on", c.codex.enabled); prefs.putString("codex_url", c.codex.endpoint);
  prefs.putString("codex_tok", c.codex.token); prefs.putString("codex_sess", c.codex.session);
  prefs.putString("codex_acct", c.codex.accountId);
  prefs.putBool("cdx_credits", c.codexCreditsFallback);
  prefs.putBool("cursor_on", c.cursor.enabled); prefs.putString("cursor_url", c.cursor.endpoint);
  prefs.putString("cursor_tok", c.cursor.token); prefs.putString("cursor_sess", c.cursor.session);
  prefs.putUChar("bright", c.brightness); prefs.putUShort("refresh", c.refreshMinutes);
  prefs.putUChar("warn_pct", c.warningPercent); prefs.putUChar("crit_pct", c.criticalPercent);
  prefs.putUInt("over_color", c.overpaceColor & 0xFFFFFF); prefs.putUInt("warn_color", c.warningColor & 0xFFFFFF);
  prefs.putUInt("bg_color", c.backgroundColor & 0xFFFFFF);
  prefs.putUInt("pace_color", c.paceIndicatorColor & 0xFFFFFF);
  prefs.putUInt("pace_glow_color", c.paceIndicatorGlowColor & 0xFFFFFF);
  prefs.putBool("pace_glow", c.paceIndicatorGlow);
  prefs.putBool("tls", c.verifyTls);
  prefs.putUChar("ui_style", c.displayStyle);
  prefs.putBool("ui_remaining", c.displayAvailable);
  prefs.putBool("off_on", c.displayOffEnabled);
  prefs.putUShort("off_from", c.displayOffFromMinutes > 1439 ? 1439 : c.displayOffFromMinutes);
  prefs.putUShort("off_until", c.displayOffUntilMinutes > 1439 ? 1439 : c.displayOffUntilMinutes);
  prefs.putBool("show_cur_main", c.showCursorModels); prefs.putBool("show_cur_other", c.showCursorOther);
  prefs.putBool("show_cur_od", c.showCursorOnDemand); prefs.putBool("show_cur_30m", c.showCursorThirtyMinute);
  prefs.remove("codex_evt_url");
  prefs.putBool("show_cdx_5h", c.showCodexFiveHour); prefs.putBool("show_cdx_7d", c.showCodexWeekly); prefs.putBool("show_cdx_30m", c.showCodexThirtyMinute);
  prefs.putUChar("ui_ver", UI_CONFIG_VERSION);
  prefs.end();
  return true;
}

bool saveWifiConfig(AppConfig &c, const String &ssid, const String &password) {
  String cleanSsid = ssid;
  cleanSsid.trim();
  if (!cleanSsid.length() || cleanSsid.length() > 32 || password.length() > 64) return false;
  if (!prefs.begin("espusage", false)) return false;
  bool ok = prefs.putString("ssid", cleanSsid) > 0;
  prefs.putString("wifi_pw", password);
  ok = prefs.putBool("wifi_saved", true) && ok;
  prefs.end();
  if (ok) { c.wifiSsid = cleanSsid; c.wifiPassword = password; c.wifiProvisioned = true; }
  return ok;
}

bool eraseWifiConfig(AppConfig &c) {
  if (!prefs.begin("espusage", false)) return false;
  bool ok = prefs.remove("ssid") || !prefs.isKey("ssid");
  prefs.remove("wifi_pw");
  prefs.remove("wifi_saved");
  prefs.end();
  c.wifiSsid = ""; c.wifiPassword = ""; c.wifiProvisioned = false;
  return ok;
}

void eraseConfig() { if (prefs.begin("espusage", false)) { prefs.clear(); prefs.end(); } }

