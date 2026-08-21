#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include "AppConfig.h"
#include "Display.h"
#include "WebPortal.h"
#include "providers/CodexProvider.h"
#include "providers/CursorProvider.h"
static AppConfig config; static CodexProvider codex; static CursorProvider cursor; static UsageSnapshot cs,us; static uint32_t lastFetch=0;
static volatile uint8_t lastDisconnectReason=0;
#define ESPUSAGE_STRINGIFY_INNER(value) #value
#define ESPUSAGE_STRINGIFY(value) ESPUSAGE_STRINGIFY_INNER(value)
#ifdef ESPUSAGE_WIFI_SSID
static void applyBuildWifi() {
  config.wifiSsid = ESPUSAGE_STRINGIFY(ESPUSAGE_WIFI_SSID);
  #ifdef ESPUSAGE_WIFI_PASSWORD
  config.wifiPassword = ESPUSAGE_STRINGIFY(ESPUSAGE_WIFI_PASSWORD);
  #endif
}
#else
static void applyBuildWifi() {}
#endif
static void connectWifi(){
  if(!config.wifiSsid.length()){
    Serial.println("[wifi] No SSID configured; starting ESPUsage-Setup");
    WiFi.mode(WIFI_AP);bool ok=WiFi.softAP("ESPUsage-Setup");
    Serial.printf("[wifi] Setup AP: %s, IP: %s\n",ok?"started":"FAILED",WiFi.softAPIP().toString().c_str());
    displaySetNetwork("SETUP 192.168.4.1",false);return;
  }
  Serial.printf("[wifi] Connecting to SSID: %s (password length: %u)\n",config.wifiSsid.c_str(),config.wifiPassword.length());
  WiFi.mode(WIFI_STA);
  WiFi.onEvent([](WiFiEvent_t event,WiFiEventInfo_t info){
    if(event==ARDUINO_EVENT_WIFI_STA_DISCONNECTED){lastDisconnectReason=info.wifi_sta_disconnected.reason;Serial.printf("[wifi] Disconnected, reason=%u\n",lastDisconnectReason);}
  });
  WiFi.setHostname(config.hostname.c_str());WiFi.begin(config.wifiSsid.c_str(),config.wifiPassword.c_str());
  uint32_t start=millis();while(WiFi.status()!=WL_CONNECTED&&millis()-start<15000){displayLoop();delay(10);}
  if(WiFi.status()==WL_CONNECTED){
    String ip=WiFi.localIP().toString();Serial.printf("[wifi] Connected. DHCP IP: %s, RSSI: %d dBm\n",ip.c_str(),WiFi.RSSI());
    bool mdns=MDNS.begin(config.hostname);Serial.printf("[wifi] mDNS: http://%s.local/ (%s)\n",config.hostname.c_str(),mdns?"ready":"failed");
    configTime(0,0,"pool.ntp.org","time.cloudflare.com");displaySetNetwork(ip,true);
  }else{
    Serial.printf("[wifi] Connection failed, status=%d, last reason=%u; starting ESPUsage-Recovery\n",(int)WiFi.status(),lastDisconnectReason);
    WiFi.mode(WIFI_AP_STA);bool ok=WiFi.softAP("ESPUsage-Recovery");
    Serial.printf("[wifi] Recovery AP: %s, IP: %s\n",ok?"started":"FAILED",WiFi.softAPIP().toString().c_str());displaySetNetwork("RECOVERY 192.168.4.1",false);
  }
}
void setup(){Serial.begin(115200);delay(300);Serial.println("\n[boot] ESP Usage starting");loadConfig(config);applyBuildWifi();displayBegin();displaySetBrightness(config.brightness);connectWifi();webBegin(config);Serial.println("[boot] Web portal ready");}
void loop(){displayLoop();webLoop();if(WiFi.status()==WL_CONNECTED&&(lastFetch==0||millis()-lastFetch>(uint32_t)config.refreshMinutes*60000UL)){lastFetch=millis();cs=codex.fetch(config.codex,config.verifyTls);us=cursor.fetch(config.cursor,config.verifyTls);displayUpdate(cs,us,config.warningPercent,config.criticalPercent);}delay(5);}

