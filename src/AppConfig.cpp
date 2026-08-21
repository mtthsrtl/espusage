#include "AppConfig.h"
#include <Preferences.h>

static Preferences prefs;

bool loadConfig(AppConfig &c) {
  if (!prefs.begin("espusage", true)) return false;
  c.wifiSsid = prefs.getString("ssid", "");
  c.wifiPassword = prefs.getString("wifi_pw", "");
  c.hostname = prefs.getString("host", "espusage");
  c.codex.enabled = prefs.getBool("codex_on", false);
  c.codex.endpoint = prefs.getString("codex_url", "");
  c.codex.token = prefs.getString("codex_tok", "");
  c.codex.session = prefs.getString("codex_sess", "");
  c.cursor.enabled = prefs.getBool("cursor_on", false);
  c.cursor.endpoint = prefs.getString("cursor_url", "https://api.cursor.com/teams/daily-usage-data");
  c.cursor.token = prefs.getString("cursor_tok", "");
  c.cursor.session = prefs.getString("cursor_sess", "");
  c.brightness = prefs.getUChar("bright", 85);
  c.refreshMinutes = prefs.getUShort("refresh", 5);
  c.verifyTls = prefs.getBool("tls", true);
  prefs.end();
  return true;
}

bool saveConfig(const AppConfig &c) {
  if (!prefs.begin("espusage", false)) return false;
  prefs.putString("ssid", c.wifiSsid); prefs.putString("wifi_pw", c.wifiPassword);
  prefs.putString("host", c.hostname);
  prefs.putBool("codex_on", c.codex.enabled); prefs.putString("codex_url", c.codex.endpoint);
  prefs.putString("codex_tok", c.codex.token); prefs.putString("codex_sess", c.codex.session);
  prefs.putBool("cursor_on", c.cursor.enabled); prefs.putString("cursor_url", c.cursor.endpoint);
  prefs.putString("cursor_tok", c.cursor.token); prefs.putString("cursor_sess", c.cursor.session);
  prefs.putUChar("bright", c.brightness); prefs.putUShort("refresh", c.refreshMinutes);
  prefs.putBool("tls", c.verifyTls); prefs.end();
  return true;
}

void eraseConfig() { if (prefs.begin("espusage", false)) { prefs.clear(); prefs.end(); } }

