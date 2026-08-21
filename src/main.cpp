#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include "AppConfig.h"
#include "Display.h"
#include "WebPortal.h"
#include "providers/CodexProvider.h"
#include "providers/CursorProvider.h"
static AppConfig config; static CodexProvider codex; static CursorProvider cursor; static UsageSnapshot cs,us; static uint32_t lastFetch=0;
static void connectWifi(){if(!config.wifiSsid.length()){WiFi.mode(WIFI_AP);WiFi.softAP("ESPUsage-Setup");displaySetNetwork("SETUP 192.168.4.1",false);return;}WiFi.mode(WIFI_STA);WiFi.setHostname(config.hostname.c_str());WiFi.begin(config.wifiSsid.c_str(),config.wifiPassword.c_str());uint32_t start=millis();while(WiFi.status()!=WL_CONNECTED&&millis()-start<15000){displayLoop();delay(10);}if(WiFi.status()==WL_CONNECTED){MDNS.begin(config.hostname);displaySetNetwork(WiFi.localIP().toString(),true);}else{WiFi.mode(WIFI_AP_STA);WiFi.softAP("ESPUsage-Recovery");displaySetNetwork("RECOVERY AP",false);}}
void setup(){Serial.begin(115200);loadConfig(config);displayBegin();displaySetBrightness(config.brightness);connectWifi();webBegin(config);}
void loop(){displayLoop();webLoop();if(WiFi.status()==WL_CONNECTED&&(lastFetch==0||millis()-lastFetch>(uint32_t)config.refreshMinutes*60000UL)){lastFetch=millis();cs=codex.fetch(config.codex,config.verifyTls);us=cursor.fetch(config.cursor,config.verifyTls);displayUpdate(cs,us);}delay(5);}

