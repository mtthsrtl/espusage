#include "WebPortal.h"
#include <WebServer.h>
#include <Update.h>
#include <WiFi.h>
#include <ArduinoJson.h>

static WebServer server(80);
static AppConfig *cfg;
static bool isSetupMode;

static const char PAGE[] PROGMEM=R"HTML(
<!doctype html><html><head><meta name=viewport content="width=device-width,initial-scale=1"><title>ESP Usage</title>
<style>
:root{color-scheme:dark}*{box-sizing:border-box}body{font:15px system-ui;background:#090c11;color:#edf2f7;max-width:760px;margin:30px auto;padding:18px}section{background:#131820;border:1px solid #2a323d;border-radius:16px;padding:20px;margin:14px 0}h1{font-size:26px;margin-bottom:6px}h2{font-size:18px}p{color:#aeb8c4;line-height:1.5}label{display:block;color:#aeb8c4;margin:12px 0 5px}input,select{width:100%;padding:12px;border-radius:9px;border:1px solid #354153;background:#0d1117;color:#fff}input[type=checkbox]{width:auto}button{padding:12px 18px;border:0;border-radius:9px;background:#10a37f;color:#fff;font-weight:700;margin-top:16px;cursor:pointer}button.secondary{background:#263241}button.danger{background:#2a323d;color:#f2b8b5}.row{display:flex;gap:10px;align-items:end}.row>div{flex:1}.grid{display:grid;grid-template-columns:1fr 1fr;gap:12px}.msg{min-height:20px;color:#72d7b5}.warn{color:#f4a62a}.setup .advanced{display:none}@media(max-width:600px){.grid{display:block}.row{display:block}}
</style></head><body><h1>ESP Usage</h1><p id=intro>Configure the device on your local network.</p>
<section><h2>Settings / WiFi</h2><p>Choose a detected network, enter its password, then save. Credentials are stored only in ESP32 NVS.</p>
<div class=row><div><label for=networks>Available networks</label><select id=networks><option value="">Scan for networks...</option></select></div><button class=secondary type=button onclick=scan()>Scan again</button></div>
<label for=manual>SSID (or enter a hidden network)</label><input id=manual maxlength=32 autocomplete=off>
<label for=wifi_pw>WiFi password</label><input id=wifi_pw type=password maxlength=64 autocomplete=new-password>
<button type=button onclick=saveWifi()>Save WiFi &amp; restart</button> <button class=danger type=button onclick=resetWifi()>Delete WiFi configuration</button><p class=msg id=wifiMsg></p></section>
<div class=advanced><form method=post action=/api/config><div class=grid><section><h2>Codex</h2><p class=warn>Direct Codex usage is unofficial and the access token expires.</p><label><input type=checkbox name=codex_on> Enabled</label><label>Codex access_token (empty keeps current)</label><input type=password name=codex_tok autocomplete=new-password><label>ChatGPT account_id, optional (empty keeps current)</label><input type=password name=codex_acct autocomplete=off><label>Custom adapter URL (empty uses Codex app token)</label><input name=codex_url></section>
<section><h2>Cursor personal</h2><p class=warn>Unofficial read-only dashboard API; may change.</p><label><input type=checkbox name=cursor_on> Enabled</label><label>Usage URL</label><input name=cursor_url value="https://cursor.com/api/usage-summary"><label>Cursor access token (empty keeps current)</label><input type=password name=cursor_tok autocomplete=new-password><p>Read from Cursor's state.vscdb key cursorAuth/accessToken.</p></section></div><button>Save Cursor &amp; Codex settings &amp; restart</button>
<section><h2>Display and status</h2><label>Hostname</label><input name=host value=espusage><label>Brightness %</label><input name=bright type=number min=1 max=100 value=85><label>Refresh minutes</label><input name=refresh type=number min=1 max=1440 value=5><label>Warning from %</label><input name=warning type=number min=1 max=98 value=70><label>Critical from %</label><input name=critical type=number min=2 max=100 value=90><button>Save settings &amp; restart</button></section></form>
<section><h2>Firmware update</h2><form method=post action=/api/ota enctype=multipart/form-data><input type=file name=firmware accept=.bin><button>Install OTA</button></form></section></div>
<script>
const q=s=>document.querySelector(s), msg=t=>q('#wifiMsg').textContent=t;
q('#networks').onchange=()=>{if(q('#networks').value)q('#manual').value=q('#networks').value};
async function scan(){msg('Scanning...');q('#networks').innerHTML='<option value="">Scanning...</option>';try{let r=await fetch('/api/wifi/scan'),a=await r.json();q('#networks').innerHTML='<option value="">Select a network</option>';a.forEach(n=>{let o=document.createElement('option');o.value=n.ssid;o.textContent=`${n.secure?'🔒 ':' '}${n.ssid} (${n.rssi} dBm)`;q('#networks').append(o)});msg(a.length?`${a.length} network(s) found`:'No networks found; you can enter the SSID manually.')}catch(e){msg('Scan failed. Try again.')}}
async function saveWifi(){let ssid=q('#manual').value.trim(),password=q('#wifi_pw').value;if(!ssid){msg('Select or enter a network first.');return}msg('Saving...');let b=new URLSearchParams({ssid,password});let r=await fetch('/api/wifi',{method:'POST',body:b});msg(await r.text())}
async function resetWifi(){if(!confirm('Delete the saved WiFi configuration and start setup mode?'))return;msg('Deleting...');let r=await fetch('/api/wifi',{method:'DELETE'});msg(await r.text())}
fetch('/api/status').then(r=>r.json()).then(s=>{if(s.setup_mode){document.body.classList.add('setup');q('#intro').textContent='Setup mode: choose your WiFi network to continue.'}else if(s.ssid){q('#manual').value=s.ssid}q('[name=codex_on]').checked=!!s.codex_configured;q('[name=cursor_on]').checked=!!s.cursor_configured;q('[name=codex_url]').value=s.codex_endpoint||'';q('[name=cursor_url]').value=s.cursor_endpoint||'https://cursor.com/api/usage-summary';q('[name=host]').value=s.hostname||'espusage';q('[name=bright]').value=s.brightness||85;q('[name=refresh]').value=s.refresh_minutes||5;q('[name=warning]').value=s.warning_percent||70;q('[name=critical]').value=s.critical_percent||90});scan();
</script></body></html>)HTML";

static void restartAfterResponse(const String &message) {
  server.send(200,"text/plain",message);
  delay(500);
  ESP.restart();
}

void webBegin(AppConfig &c, bool setupMode){
 cfg=&c; isSetupMode=setupMode;
 server.on("/",HTTP_GET,[]{server.send_P(200,"text/html",PAGE);});
 server.on("/api/status",HTTP_GET,[]{JsonDocument d;d["firmware"]="espusage";d["setup_mode"]=isSetupMode;d["ssid"]=cfg->wifiProvisioned?cfg->wifiSsid:"";d["ip"]=isSetupMode?WiFi.softAPIP().toString():WiFi.localIP().toString();d["rssi"]=isSetupMode?0:WiFi.RSSI();d["uptime_ms"]=millis();d["free_heap"]=ESP.getFreeHeap();d["psram_free"]=ESP.getFreePsram();d["hostname"]=cfg->hostname;d["codex_configured"]=cfg->codex.enabled;d["codex_token_stored"]=cfg->codex.token.length()>0;d["codex_endpoint"]=cfg->codex.endpoint;d["cursor_configured"]=cfg->cursor.enabled;d["cursor_token_stored"]=cfg->cursor.token.length()>0;d["cursor_endpoint"]=cfg->cursor.endpoint;d["brightness"]=cfg->brightness;d["refresh_minutes"]=cfg->refreshMinutes;d["warning_percent"]=cfg->warningPercent;d["critical_percent"]=cfg->criticalPercent;String s;serializeJson(d,s);server.send(200,"application/json",s);});
 server.on("/api/health",HTTP_GET,[]{server.send(200,"application/json","{\"ok\":true}");});
 server.on("/api/wifi/scan",HTTP_GET,[]{
   Serial.println("[wifi][scan] Scanning for access points");
   int count=WiFi.scanNetworks(false,true);JsonDocument d;JsonArray a=d.to<JsonArray>();
   for(int i=0;i<count;i++){String ssid=WiFi.SSID(i);bool duplicate=false;for(JsonObject n:a){if(n["ssid"].as<String>()==ssid){duplicate=true;break;}}if(!ssid.length()||duplicate)continue;JsonObject n=a.add<JsonObject>();n["ssid"]=ssid;n["rssi"]=WiFi.RSSI(i);n["secure"]=WiFi.encryptionType(i)!=WIFI_AUTH_OPEN;}
   WiFi.scanDelete();String s;serializeJson(d,s);Serial.printf("[wifi][scan] Found %u unique networks\n",a.size());server.send(200,"application/json",s);
 });
 server.on("/api/wifi",HTTP_POST,[]{String ssid=server.arg("ssid"),password=server.arg("password");if(!saveWifiConfig(*cfg,ssid,password)){server.send(400,"text/plain","Invalid WiFi data or NVS write failed.");return;}Serial.printf("[wifi][nvs] Saved credentials for SSID '%s'; restarting\n",cfg->wifiSsid.c_str());restartAfterResponse("WiFi saved. Restarting...");});
 server.on("/api/wifi",HTTP_DELETE,[]{if(!eraseWifiConfig(*cfg)){server.send(500,"text/plain","Could not clear WiFi configuration.");return;}Serial.println("[wifi][nvs] WiFi credentials deleted; restarting into setup mode");restartAfterResponse("WiFi configuration deleted. Restarting...");});
 server.on("/api/config",HTTP_POST,[]{cfg->hostname=server.arg("host");cfg->codex.enabled=server.hasArg("codex_on");cfg->codex.endpoint=server.arg("codex_url");String codexToken=server.arg("codex_tok");codexToken.trim();if(codexToken.startsWith("Bearer "))codexToken.remove(0,7);if(codexToken.length())cfg->codex.token=codexToken;String accountId=server.arg("codex_acct");accountId.trim();if(accountId.length())cfg->codex.accountId=accountId;cfg->cursor.enabled=server.hasArg("cursor_on");cfg->cursor.endpoint=server.arg("cursor_url");String cursorToken=server.arg("cursor_tok");cursorToken.trim();if(cursorToken.length())cfg->cursor.token=cursorToken;cfg->brightness=constrain(server.arg("bright").toInt(),1,100);cfg->refreshMinutes=constrain(server.arg("refresh").toInt(),1,1440);cfg->warningPercent=constrain(server.arg("warning").toInt(),1,98);cfg->criticalPercent=constrain(server.arg("critical").toInt(),cfg->warningPercent+1,100);Serial.printf("[config][nvs] Saving Cursor: enabled=%s, token=%s\n",cfg->cursor.enabled?"yes":"no",cfg->cursor.token.length()?"stored":"missing");Serial.printf("[config][nvs] Saving Codex: enabled=%s, access_token=%s, account_id=%s\n",cfg->codex.enabled?"yes":"no",cfg->codex.token.length()?"stored":"missing",cfg->codex.accountId.length()?"stored":"missing");if(!saveConfig(*cfg)){server.send(500,"text/plain","Could not save settings.");return;}restartAfterResponse("Settings saved. Restarting...");});
 server.on("/api/ota",HTTP_POST,[]{bool ok=!Update.hasError();server.send(ok?200:500,"text/plain",ok?"Update complete; restarting":"Update failed");delay(400);if(ok)ESP.restart();},[]{HTTPUpload &u=server.upload();if(u.status==UPLOAD_FILE_START)Update.begin(UPDATE_SIZE_UNKNOWN,U_FLASH);else if(u.status==UPLOAD_FILE_WRITE)Update.write(u.buf,u.currentSize);else if(u.status==UPLOAD_FILE_END)Update.end(true);});
 server.onNotFound([](){if(isSetupMode){server.sendHeader("Location","http://192.168.4.1/",true);server.send(302,"text/plain","");}else server.send(404,"text/plain","Not found");});
 server.begin();
}
void webLoop(){server.handleClient();}

