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
static String startupNetworkText="STARTING"; static bool startupNetworkConnected=false;
#define ESPUSAGE_STRINGIFY_INNER(value) #value
#define ESPUSAGE_STRINGIFY(value) ESPUSAGE_STRINGIFY_INNER(value)
static String normalizeBuildValue(const char *raw){
  String v=raw?String(raw):String();
  if(v.length()>=2){char a=v.charAt(0),b=v.charAt(v.length()-1);if((a=='\"'&&b=='\"')||(a=='\''&&b=='\''))v=v.substring(1,v.length()-1);}
  return v;
}
#ifdef ESPUSAGE_WIFI_SSID
static void applyBuildWifi() {
  if (config.wifiProvisioned) {
    Serial.println("[wifi] Using WiFi credentials saved in the web portal");
    return;
  }
  config.wifiSsid = normalizeBuildValue(ESPUSAGE_STRINGIFY(ESPUSAGE_WIFI_SSID));
  #ifdef ESPUSAGE_WIFI_PASSWORD
  config.wifiPassword = normalizeBuildValue(ESPUSAGE_STRINGIFY(ESPUSAGE_WIFI_PASSWORD));
  #endif
  Serial.printf("[wifi] Using build-time WiFi credentials (SSID length: %u, password length: %u)\n",config.wifiSsid.length(),config.wifiPassword.length());
}
#else
static void applyBuildWifi() {}
#endif
static void startRecoveryAp(const char *name){
  WiFi.disconnect(true,false);delay(150);WiFi.mode(WIFI_AP);bool ok=WiFi.softAP(name);
  Serial.printf("[wifi] Recovery AP: %s, IP: %s\n",ok?"started":"FAILED",WiFi.softAPIP().toString().c_str());startupNetworkText=String(name).indexOf("Setup")>=0?"SETUP 192.168.4.1":"RECOVERY 192.168.4.1";startupNetworkConnected=false;
}
static void connectWifi(){
  if(!config.wifiSsid.length()){
    Serial.println("[wifi] No SSID configured; starting ESPUsage-Setup");startRecoveryAp("ESPUsage-Setup");return;
  }
  Serial.printf("[wifi] Connecting to SSID: %s (password length: %u)\n",config.wifiSsid.c_str(),config.wifiPassword.length());
  WiFi.persistent(false);WiFi.mode(WIFI_STA);WiFi.setSleep(false);WiFi.setAutoReconnect(true);
  WiFi.onEvent([](WiFiEvent_t event,WiFiEventInfo_t info){
    if(event==ARDUINO_EVENT_WIFI_STA_DISCONNECTED){lastDisconnectReason=info.wifi_sta_disconnected.reason;Serial.printf("[wifi] Disconnected, reason=%u\n",lastDisconnectReason);}
  });
  WiFi.setHostname(config.hostname.c_str());
  for(uint8_t attempt=1;attempt<=3&&WiFi.status()!=WL_CONNECTED;attempt++){
    lastDisconnectReason=0;Serial.printf("[wifi] Attempt %u/3\n",attempt);WiFi.begin(config.wifiSsid.c_str(),config.wifiPassword.c_str());
    uint32_t start=millis();while(WiFi.status()!=WL_CONNECTED&&millis()-start<10000){delay(25);}
    if(WiFi.status()!=WL_CONNECTED){WiFi.disconnect(false,false);delay(300);}
  }
  if(WiFi.status()==WL_CONNECTED){
    String ip=WiFi.localIP().toString();Serial.printf("[wifi] Connected. DHCP IP: %s, RSSI: %d dBm, channel: %d\n",ip.c_str(),WiFi.RSSI(),WiFi.channel());
    bool mdns=MDNS.begin(config.hostname);Serial.printf("[wifi] mDNS: http://%s.local/ (%s)\n",config.hostname.c_str(),mdns?"ready":"failed");
    configTime(0,0,"pool.ntp.org","time.cloudflare.com");startupNetworkText=ip;startupNetworkConnected=true;
  }else{
    Serial.printf("[wifi] Connection failed, status=%d, last reason=%u; starting ESPUsage-Recovery\n",(int)WiFi.status(),lastDisconnectReason);
    if(lastDisconnectReason==15)Serial.println("[wifi] reason=15: WPA 4-way handshake timeout; verify the exact password and AP security mode");
    if(lastDisconnectReason==2)Serial.println("[wifi] reason=2: authentication expired/timed out");
    startRecoveryAp("ESPUsage-Recovery");
  }
}
void setup(){Serial.begin(115200);delay(300);Serial.println("\n[boot] ESP Usage starting");loadConfig(config);applyBuildWifi();connectWifi();displayBegin();displaySetBrightness(config.brightness);displaySetNetwork(startupNetworkText,startupNetworkConnected);webBegin(config);Serial.println("[boot] Web portal ready");}
void loop(){displayLoop();webLoop();if(WiFi.status()==WL_CONNECTED&&(lastFetch==0||millis()-lastFetch>(uint32_t)config.refreshMinutes*60000UL)){lastFetch=millis();cs=codex.fetch(config.codex,config.verifyTls);us=cursor.fetch(config.cursor,config.verifyTls);displayUpdate(cs,us,config.warningPercent,config.criticalPercent);}delay(5);}
