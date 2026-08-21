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

struct CodexPaceBucket { uint32_t slot = 0; float delta = 0; bool valid = false; };
static CodexPaceBucket codexPace[6];
static bool codexLastValid = false;
static float codexLastWeekly = -1;
static uint16_t codexSampleCount = 0;

static void clearCodexPace() {
  for (CodexPaceBucket &bucket : codexPace) bucket = CodexPaceBucket();
}

static void updateCodexPace(UsageSnapshot &snapshot) {
  RecentUsage30m &recent = snapshot.recent30m;
  time_t now = time(nullptr);
  if (now < 1700000000) { recent.status = "waiting for clock"; return; }
  float weekly = snapshot.secondary.usedPercent;
  if (weekly < 0) { recent.status = "weekly data missing"; return; }

  uint32_t slot = (uint32_t)now / 300U;
  CodexPaceBucket &current = codexPace[slot % 6];
  if (!current.valid || current.slot != slot) { current.slot = slot; current.delta = 0; current.valid = true; }
  bool resetDetected = codexLastValid && weekly + 0.05f < codexLastWeekly;
  if (resetDetected) {
    clearCodexPace();
    codexSampleCount = 1;
    CodexPaceBucket &resetBucket = codexPace[slot % 6];
    resetBucket.slot = slot; resetBucket.valid = true;
    Serial.printf("[usage][codex][30m] Weekly reset detected: %.1f -> %.1f\n", codexLastWeekly, weekly);
  } else {
    if (codexSampleCount < UINT16_MAX) codexSampleCount++;
    if (codexLastValid) {
      float delta = weekly - codexLastWeekly;
      if (delta > 0) codexPace[slot % 6].delta += delta;
    }
  }
  codexLastWeekly = weekly; codexLastValid = true;

  recent.available = true;
  uint8_t validCount = 0;
  uint32_t firstSlot = slot >= 5 ? slot - 5 : 0;
  for (uint8_t i = 0; i < 6; ++i) {
    uint32_t wanted = firstSlot + i;
    CodexPaceBucket &source = codexPace[wanted % 6];
    if (source.valid && source.slot == wanted) {
      recent.buckets[i].valid = true;
      recent.buckets[i].deltaPercent = source.delta;
      recent.deltaPercent += source.delta;
      validCount++;
    }
  }
  recent.samples = codexSampleCount;
  recent.ready = codexSampleCount >= 2;
  recent.status = resetDetected ? "reset detected; collecting baseline" : recent.ready
                    ? validCount == 6 ? "online" : "online; " + String(validCount) + "/6 buckets"
                    : "collecting baseline";
  Serial.printf("[usage][codex][30m] delta=%.2f pp, measurements=%u, buckets=%u/6, status=%s\n",
                recent.deltaPercent, recent.samples, validCount, recent.status.c_str());
}
static void startRecoveryAp(const char *name){
  WiFi.disconnect(false,false);delay(150);WiFi.mode(WIFI_AP_STA);bool ok=WiFi.softAP(name);
  Serial.printf("[wifi][setup] AP '%s': %s\n",name,ok?"started":"FAILED");
  Serial.printf("[wifi][setup] Portal: http://%s/\n",WiFi.softAPIP().toString().c_str());
  startupNetworkText=String(name).indexOf("Setup")>=0?"SETUP 192.168.4.1":"RECOVERY 192.168.4.1";startupNetworkConnected=false;
}
static bool connectWifi(){
  Serial.println("[wifi][nvs] Loading Wi-Fi credentials");
  if(!config.wifiProvisioned || !config.wifiSsid.length()){
    Serial.println("[wifi][nvs] No valid credentials stored");startRecoveryAp("ESPUsage-Setup");return false;
  }
  Serial.printf("[wifi][nvs] Credentials found for SSID '%s'\n",config.wifiSsid.c_str());
  WiFi.persistent(false);WiFi.mode(WIFI_STA);WiFi.setSleep(false);WiFi.setAutoReconnect(true);
  WiFi.onEvent([](WiFiEvent_t event,WiFiEventInfo_t info){
    if(event==ARDUINO_EVENT_WIFI_STA_DISCONNECTED){lastDisconnectReason=info.wifi_sta_disconnected.reason;Serial.printf("[wifi][station] Disconnected, reason=%u\n",lastDisconnectReason);}
  });
  WiFi.setHostname(config.hostname.c_str());
  for(uint8_t attempt=1;attempt<=3&&WiFi.status()!=WL_CONNECTED;attempt++){
    lastDisconnectReason=0;Serial.printf("[wifi][station] Connecting to '%s', attempt %u/3\n",config.wifiSsid.c_str(),attempt);WiFi.begin(config.wifiSsid.c_str(),config.wifiPassword.c_str());
    uint32_t start=millis();while(WiFi.status()!=WL_CONNECTED&&millis()-start<10000){delay(25);}
    if(WiFi.status()!=WL_CONNECTED){Serial.printf("[wifi][station] Attempt %u failed, status=%d, reason=%u\n",attempt,(int)WiFi.status(),lastDisconnectReason);WiFi.disconnect(false,false);delay(300);}
  }
  if(WiFi.status()==WL_CONNECTED){
    String ip=WiFi.localIP().toString();Serial.printf("[wifi][station] Connected. DHCP IP: %s, RSSI: %d dBm, channel: %d\n",ip.c_str(),WiFi.RSSI(),WiFi.channel());
    bool mdns=MDNS.begin(config.hostname);Serial.printf("[wifi] mDNS: http://%s.local/ (%s)\n",config.hostname.c_str(),mdns?"ready":"failed");
    configTime(0,0,"pool.ntp.org","time.cloudflare.com");startupNetworkText=ip;startupNetworkConnected=true;return true;
  }else{
    Serial.printf("[wifi][station] All attempts failed, status=%d, last reason=%u\n",(int)WiFi.status(),lastDisconnectReason);
    if(lastDisconnectReason==15)Serial.println("[wifi][station] reason=15: WPA 4-way handshake timeout; check password/security mode");
    if(lastDisconnectReason==2)Serial.println("[wifi][station] reason=2: authentication expired/timed out");
    Serial.println("[wifi][setup] Falling back to recovery portal");startRecoveryAp("ESPUsage-Setup");return false;
  }
}
void setup(){Serial.begin(115200);delay(300);Serial.println("\n[boot] ESP Usage starting");loadConfig(config);Serial.printf("[config][nvs] Cursor: enabled=%s, token=%s\n",config.cursor.enabled?"yes":"no",config.cursor.token.length()?"stored":"missing");Serial.printf("[config][nvs] Codex: enabled=%s, access_token=%s, account_id=%s, mode=%s\n",config.codex.enabled?"yes":"no",config.codex.token.length()?"stored":"missing",config.codex.accountId.length()?"stored":"missing",config.codex.endpoint.length()?"adapter":"direct");bool connected=connectWifi();displayBegin(config);displaySetBrightness(config.brightness);displaySetNetwork(startupNetworkText,startupNetworkConnected);webBegin(config,!connected);Serial.println("[boot] Web portal ready");}
void loop(){
  displayLoop(); webLoop();
  if(WiFi.status()==WL_CONNECTED&&(lastFetch==0||millis()-lastFetch>(uint32_t)config.refreshMinutes*60000UL)){
    lastFetch=millis(); Serial.println("[usage] Refreshing Codex and Cursor");
    cs=codex.fetch(config.codex,config.verifyTls); updateCodexPace(cs); Serial.printf("[usage][codex] %s\n",cs.status.c_str());
    us=cursor.fetch(config.cursor,config.verifyTls); Serial.printf("[usage][cursor] %s\n",us.status.c_str());
    displayUpdate(cs,us,config.warningPercent,config.criticalPercent); webUpdateUsage(cs,us);
  }
  delay(5);
}
