#include "WebPortal.h"
#include <WebServer.h>
#include <Update.h>
#include <WiFi.h>
#include <ArduinoJson.h>

static WebServer server(80);
static AppConfig *cfg;
static bool isSetupMode;
static UsageSnapshot latestCodex;
static UsageSnapshot latestCursor;
static bool hasUsageSnapshot = false;

static const char PAGE[] PROGMEM=R"HTML(
<!doctype html><html><head><meta name=viewport content="width=device-width,initial-scale=1"><title>ESP Usage</title>
<style>
:root{color-scheme:dark}*{box-sizing:border-box}body{font:15px system-ui;background:#090c11;color:#edf2f7;max-width:760px;margin:30px auto;padding:18px}section{background:#131820;border:1px solid #2a323d;border-radius:16px;padding:20px;margin:14px 0}h1{font-size:26px;margin-bottom:6px}h2{font-size:18px}p{color:#aeb8c4;line-height:1.5}label{display:block;color:#aeb8c4;margin:12px 0 5px}input,select{width:100%;padding:12px;border-radius:9px;border:1px solid #354153;background:#0d1117;color:#fff}input[type=checkbox]{width:auto}button{padding:12px 18px;border:0;border-radius:9px;background:#10a37f;color:#fff;font-weight:700;margin-top:16px;cursor:pointer}button.secondary{background:#263241}button.danger{background:#2a323d;color:#f2b8b5}.row{display:flex;gap:10px;align-items:end}.row>div{flex:1}.grid,.checks{display:grid;grid-template-columns:1fr 1fr;gap:12px}.checks label{margin:4px 0}.msg{min-height:20px;color:#72d7b5}.warn{color:#f4a62a}.setup .advanced{display:none}.usage-card{background:#0d1117;border:1px solid #2a323d;border-radius:11px;padding:14px}.usage-card h3{margin:0 0 8px}.usage-main{font-size:20px;color:#fff;margin:4px 0}.usage-detail{white-space:pre-line;color:#aeb8c4;line-height:1.55;min-height:92px}.mini{height:36px;display:flex;gap:5px;align-items:end;margin:12px 0 5px}.mini span{display:block;flex:1;min-height:2px;background:#35d078;border-radius:2px 2px 0 0}.usage-card.codex .mini span{background:#8295a8}.partial{color:#f4a62a}@media(max-width:600px){.grid,.checks{display:block}.row{display:block}.usage-card{margin:10px 0}}
</style></head><body><h1>ESP Usage</h1><p id=intro>Configure the device on your local network.</p>
<section><h2>Settings / WiFi</h2><p>Choose a detected network, enter its password, then save. Credentials are stored only in ESP32 NVS.</p>
<div class=row><div><label for=networks>Available networks</label><select id=networks><option value="">Scan for networks...</option></select></div><button class=secondary type=button onclick=scan()>Scan again</button></div>
<label for=manual>SSID (or enter a hidden network)</label><input id=manual maxlength=32 autocomplete=off>
<label for=wifi_pw>WiFi password</label><input id=wifi_pw type=password maxlength=64 autocomplete=new-password>
<button type=button onclick=saveWifi()>Save WiFi &amp; restart</button> <button class=danger type=button onclick=resetWifi()>Delete WiFi configuration</button><p class=msg id=wifiMsg></p></section>
<div class=advanced><section><h2>Live usage</h2><p>Current provider values plus Cursor activity for the last 30 minutes. This view contains no access tokens or account identifiers.</p><div class=grid><div class=usage-card><h3>Cursor</h3><div class=usage-main id=cursorMain>Waiting...</div><div class=mini id=cursorChart></div><div class=usage-detail id=cursorDetail></div></div><div class="usage-card codex"><h3>Codex</h3><div class=usage-main id=codexMain>Waiting...</div><div class=usage-detail id=codexDetail></div></div></div></section>
<form method=post action=/api/config><div class=grid><section><h2>Codex</h2><p class=warn>Direct Codex limit usage is unofficial and the access token expires.</p><label><input type=checkbox name=codex_on> Enabled</label><label>Codex access_token (empty keeps current)</label><input type=password name=codex_tok autocomplete=new-password><label>ChatGPT account_id, optional (empty keeps current)</label><input type=password name=codex_acct autocomplete=off><label>Custom limit adapter URL (empty uses Codex app token)</label><input name=codex_url></section>
<section><h2>Cursor personal</h2><p class=warn>Unofficial read-only dashboard API; may change.</p><label><input type=checkbox name=cursor_on> Enabled</label><label>Usage URL</label><input name=cursor_url value="https://cursor.com/api/usage-summary"><label>Cursor access token (empty keeps current)</label><input type=password name=cursor_tok autocomplete=new-password><p>Read from Cursor's state.vscdb key cursorAuth/accessToken.</p></section></div><button>Save Cursor &amp; Codex settings &amp; restart</button>
<section><h2>Display and status</h2><label>Dashboard design</label><select name=display_style><option value=panels>Panels - framed sections</option><option value=open>Flat - open sections and larger</option></select><p>Select which usage rows are visible. The small white line on each limit bar shows how far its reset period has elapsed.</p><div class=checks><label><input type=checkbox name=show_cursor_models> Cursor Models</label><label><input type=checkbox name=show_cursor_other> Other Models</label><label><input type=checkbox name=show_cursor_ondemand> On Demand</label><label><input type=checkbox name=show_cursor_30m> Cursor 30 minutes</label><label><input type=checkbox name=show_codex_weekly> Codex weekly limit</label></div><label>Hostname</label><input name=host value=espusage><label>Brightness %</label><input name=bright type=number min=1 max=100 value=85><label>Refresh minutes</label><input name=refresh type=number min=1 max=1440 value=5><label>Warning from %</label><input name=warning type=number min=1 max=98 value=70><label>Critical from %</label><input name=critical type=number min=2 max=100 value=90><button>Save settings &amp; restart</button></section></form>
<section><h2>Firmware update</h2><form method=post action=/api/ota enctype=multipart/form-data><input type=file name=firmware accept=.bin><button>Install OTA</button></form></section></div>
<script>
const q=s=>document.querySelector(s), msg=t=>q('#wifiMsg').textContent=t;
q('#networks').onchange=()=>{if(q('#networks').value)q('#manual').value=q('#networks').value};
async function scan(){msg('Scanning...');q('#networks').innerHTML='<option value="">Scanning...</option>';try{let r=await fetch('/api/wifi/scan'),a=await r.json();q('#networks').innerHTML='<option value="">Select a network</option>';a.forEach(n=>{let o=document.createElement('option');o.value=n.ssid;o.textContent=`${n.secure?'🔒 ':' '}${n.ssid} (${n.rssi} dBm)`;q('#networks').append(o)});msg(a.length?`${a.length} network(s) found`:'No networks found; you can enter the SSID manually.')}catch(e){msg('Scan failed. Try again.')}}
async function saveWifi(){let ssid=q('#manual').value.trim(),password=q('#wifi_pw').value;if(!ssid){msg('Select or enter a network first.');return}msg('Saving...');let b=new URLSearchParams({ssid,password});let r=await fetch('/api/wifi',{method:'POST',body:b});msg(await r.text())}
async function resetWifi(){if(!confirm('Delete the saved WiFi configuration and start setup mode?'))return;msg('Deleting...');let r=await fetch('/api/wifi',{method:'DELETE'});msg(await r.text())}
const fmt=n=>{n=Number(n||0);return n>=1e9?(n/1e9).toFixed(2)+'B':n>=1e6?(n/1e6).toFixed(2)+'M':n>=1e3?(n/1e3).toFixed(1)+'K':String(n)};
function chart(id,buckets,field){let root=q(id),max=Math.max(0,...buckets.map(b=>b.valid?Number(b[field]||0):0));root.replaceChildren();buckets.forEach(b=>{let bar=document.createElement('span'),v=Number(b[field]||0);bar.style.height=(b.valid&&max?Math.max(2,Math.round(34*v/max)):2)+'px';bar.style.opacity=b.valid?'1':'.25';bar.title=b.valid?String(v):'No sample';root.append(bar)})}
function renderCursor(c){let r=c.recent_30m||{},tokens=Number(r.total_tokens||0),partial=r.tokenized_calls<r.calls||r.partial;q('#cursorMain').textContent=!r.available?'NO DATA':r.calls?(r.token_data?`${fmt(tokens)}${partial?'+':''} TOK | ${r.calls} CALLS`:`${r.calls} CALLS | TOK N/A`):'0 CALLS';q('#cursorDetail').textContent=`Models ${c.primary.used_percent<0?'--':c.primary.used_percent.toFixed(1)}% used | Other ${c.secondary.used_percent<0?'--':c.secondary.used_percent.toFixed(1)}% | On Demand ${c.tertiary.used_percent<0?'--':c.tertiary.used_percent.toFixed(1)}%\nInput ${fmt(r.input_tokens)} | Output ${fmt(r.output_tokens)}\nCache read ${fmt(r.cache_read_tokens)} | Cache write ${fmt(r.cache_write_tokens)}\nTop model ${r.top_model||'n/a'} | Type ${r.top_kind||'n/a'} | Max Mode ${r.max_mode_calls||0}${r.cost_cents>=0?' | Cost $'+(r.cost_cents/100).toFixed(4):''}\n${partial?'Partial token coverage: some calls did not report tokens.':(r.status||c.status)}`;q('#cursorDetail').classList.toggle('partial',partial);chart('#cursorChart',r.buckets||[],r.token_data&&tokens>0?'tokens':'calls')}
function renderCodex(c){let w=c.secondary||{},used=Number(w.used_percent),ok=Number.isFinite(used)&&used>=0;q('#codexMain').textContent=ok?`${used.toFixed(1)}% USED`:'NO DATA';q('#codexDetail').textContent=`Weekly limit\n${w.reset||'Reset time unavailable'}\n${c.status||'Unavailable'}`}
async function loadUsage(){try{let r=await fetch('/api/usage'),u=await r.json();if(u.ready){renderCursor(u.cursor);renderCodex(u.codex)}}catch(e){q('#cursorMain').textContent=q('#codexMain').textContent='Unavailable'}}
fetch('/api/status').then(r=>r.json()).then(s=>{if(s.setup_mode){document.body.classList.add('setup');q('#intro').textContent='Setup mode: choose your WiFi network to continue.'}else if(s.ssid){q('#manual').value=s.ssid}q('[name=codex_on]').checked=!!s.codex_configured;q('[name=cursor_on]').checked=!!s.cursor_configured;q('[name=codex_url]').value=s.codex_endpoint||'';q('[name=cursor_url]').value=s.cursor_endpoint||'https://cursor.com/api/usage-summary';q('[name=display_style]').value=s.display_style||'panels';q('[name=show_cursor_models]').checked=!!s.show_cursor_models;q('[name=show_cursor_other]').checked=!!s.show_cursor_other;q('[name=show_cursor_ondemand]').checked=!!s.show_cursor_ondemand;q('[name=show_cursor_30m]').checked=!!s.show_cursor_30m;q('[name=show_codex_weekly]').checked=!!s.show_codex_weekly;q('[name=host]').value=s.hostname||'espusage';q('[name=bright]').value=s.brightness||85;q('[name=refresh]').value=s.refresh_minutes||5;q('[name=warning]').value=s.warning_percent||70;q('[name=critical]').value=s.critical_percent||90});scan();loadUsage();setInterval(loadUsage,15000);
</script></body></html>)HTML";

static void addWindowJson(JsonObject target, const UsageWindow &window) {
  target["label"] = window.label;
  target["used_percent"] = window.usedPercent;
  target["elapsed_percent"] = window.elapsedPercent;
  target["reset"] = window.resetText;
}

static void addRecentJson(JsonObject target, const RecentUsage30m &recent) {
  target["available"] = recent.available;
  target["ready"] = recent.ready;
  target["partial"] = recent.partial;
  target["token_data"] = recent.tokenData;
  target["calls"] = recent.calls;
  target["tokenized_calls"] = recent.tokenizedCalls;
  target["max_mode_calls"] = recent.maxModeCalls;
  target["samples"] = recent.samples;
  target["input_tokens"] = recent.inputTokens;
  target["output_tokens"] = recent.outputTokens;
  target["cache_read_tokens"] = recent.cacheReadTokens;
  target["cache_write_tokens"] = recent.cacheWriteTokens;
  target["total_tokens"] = recent.inputTokens + recent.outputTokens + recent.cacheReadTokens + recent.cacheWriteTokens;
  target["cost_cents"] = recent.costCents;
  target["delta_pp"] = recent.deltaPercent;
  target["top_model"] = recent.topModel;
  target["top_kind"] = recent.topKind;
  target["status"] = recent.status;
  JsonArray buckets = target["buckets"].to<JsonArray>();
  for (const RecentUsageBucket &bucket : recent.buckets) {
    JsonObject item = buckets.add<JsonObject>();
    item["valid"] = bucket.valid;
    item["tokens"] = bucket.tokens;
    item["calls"] = bucket.calls;
    item["delta_pp"] = bucket.deltaPercent;
  }
}

static void addSnapshotJson(JsonObject target, const UsageSnapshot &snapshot) {
  target["ok"] = snapshot.ok;
  target["status"] = snapshot.status;
  target["plan"] = snapshot.plan;
  target["updated"] = snapshot.updated;
  addWindowJson(target["primary"].to<JsonObject>(), snapshot.primary);
  addWindowJson(target["secondary"].to<JsonObject>(), snapshot.secondary);
  addWindowJson(target["tertiary"].to<JsonObject>(), snapshot.tertiary);
  addRecentJson(target["recent_30m"].to<JsonObject>(), snapshot.recent30m);
}

static void restartAfterResponse(const String &message) {
  server.send(200,"text/plain",message);
  delay(500);
  ESP.restart();
}

void webBegin(AppConfig &c, bool setupMode){
 cfg=&c; isSetupMode=setupMode;
 server.on("/",HTTP_GET,[]{server.send_P(200,"text/html",PAGE);});
 server.on("/api/status",HTTP_GET,[]{
   JsonDocument d;
   d["firmware"]="espusage"; d["setup_mode"]=isSetupMode; d["ssid"]=cfg->wifiProvisioned?cfg->wifiSsid:"";
   d["ip"]=isSetupMode?WiFi.softAPIP().toString():WiFi.localIP().toString(); d["rssi"]=isSetupMode?0:WiFi.RSSI();
   d["uptime_ms"]=millis(); d["free_heap"]=ESP.getFreeHeap(); d["psram_free"]=ESP.getFreePsram(); d["hostname"]=cfg->hostname;
   d["codex_configured"]=cfg->codex.enabled; d["codex_token_stored"]=cfg->codex.token.length()>0; d["codex_endpoint"]=cfg->codex.endpoint;
   d["cursor_configured"]=cfg->cursor.enabled; d["cursor_token_stored"]=cfg->cursor.token.length()>0; d["cursor_endpoint"]=cfg->cursor.endpoint;
   d["display_style"]=cfg->displayStyle==1?"open":"panels";
   d["show_cursor_models"]=cfg->showCursorModels; d["show_cursor_other"]=cfg->showCursorOther; d["show_cursor_ondemand"]=cfg->showCursorOnDemand;
   d["show_cursor_30m"]=cfg->showCursorThirtyMinute; d["show_codex_weekly"]=cfg->showCodexWeekly;
   d["brightness"]=cfg->brightness; d["refresh_minutes"]=cfg->refreshMinutes;
   d["warning_percent"]=cfg->warningPercent; d["critical_percent"]=cfg->criticalPercent;
   String s; serializeJson(d,s); server.send(200,"application/json",s);
 });
 server.on("/api/usage",HTTP_GET,[]{
   JsonDocument d; d["ready"]=hasUsageSnapshot;
   addSnapshotJson(d["cursor"].to<JsonObject>(), latestCursor);
   addSnapshotJson(d["codex"].to<JsonObject>(), latestCodex);
   String s; serializeJson(d,s); server.send(200,"application/json",s);
 });
 server.on("/api/health",HTTP_GET,[]{server.send(200,"application/json","{\"ok\":true}");});
 server.on("/api/wifi/scan",HTTP_GET,[]{
   Serial.println("[wifi][scan] Scanning for access points");
   int count=WiFi.scanNetworks(false,true);JsonDocument d;JsonArray a=d.to<JsonArray>();
   for(int i=0;i<count;i++){String ssid=WiFi.SSID(i);bool duplicate=false;for(JsonObject n:a){if(n["ssid"].as<String>()==ssid){duplicate=true;break;}}if(!ssid.length()||duplicate)continue;JsonObject n=a.add<JsonObject>();n["ssid"]=ssid;n["rssi"]=WiFi.RSSI(i);n["secure"]=WiFi.encryptionType(i)!=WIFI_AUTH_OPEN;}
   WiFi.scanDelete();String s;serializeJson(d,s);Serial.printf("[wifi][scan] Found %u unique networks\n",a.size());server.send(200,"application/json",s);
 });
 server.on("/api/wifi",HTTP_POST,[]{String ssid=server.arg("ssid"),password=server.arg("password");if(!saveWifiConfig(*cfg,ssid,password)){server.send(400,"text/plain","Invalid WiFi data or NVS write failed.");return;}Serial.printf("[wifi][nvs] Saved credentials for SSID '%s'; restarting\n",cfg->wifiSsid.c_str());restartAfterResponse("WiFi saved. Restarting...");});
 server.on("/api/wifi",HTTP_DELETE,[]{if(!eraseWifiConfig(*cfg)){server.send(500,"text/plain","Could not clear WiFi configuration.");return;}Serial.println("[wifi][nvs] WiFi credentials deleted; restarting into setup mode");restartAfterResponse("WiFi configuration deleted. Restarting...");});
 server.on("/api/config",HTTP_POST,[]{
   cfg->hostname=server.arg("host"); cfg->codex.enabled=server.hasArg("codex_on"); cfg->codex.endpoint=server.arg("codex_url");
   String codexToken=server.arg("codex_tok"); codexToken.trim(); if(codexToken.startsWith("Bearer "))codexToken.remove(0,7); if(codexToken.length())cfg->codex.token=codexToken;
   String accountId=server.arg("codex_acct"); accountId.trim(); if(accountId.length())cfg->codex.accountId=accountId;
   cfg->cursor.enabled=server.hasArg("cursor_on"); cfg->cursor.endpoint=server.arg("cursor_url"); String cursorToken=server.arg("cursor_tok"); cursorToken.trim(); if(cursorToken.length())cfg->cursor.token=cursorToken;
   cfg->displayStyle=server.arg("display_style")=="open"?1:0; cfg->showCursorModels=server.hasArg("show_cursor_models"); cfg->showCursorOther=server.hasArg("show_cursor_other");
   cfg->showCursorOnDemand=server.hasArg("show_cursor_ondemand"); cfg->showCursorThirtyMinute=server.hasArg("show_cursor_30m");
   cfg->showCodexWeekly=server.hasArg("show_codex_weekly");
   cfg->brightness=constrain(server.arg("bright").toInt(),1,100); cfg->refreshMinutes=constrain(server.arg("refresh").toInt(),1,1440);
   cfg->warningPercent=constrain(server.arg("warning").toInt(),1,98); cfg->criticalPercent=constrain(server.arg("critical").toInt(),cfg->warningPercent+1,100);
   Serial.printf("[config][nvs] Saving Cursor: enabled=%s, token=%s\n",cfg->cursor.enabled?"yes":"no",cfg->cursor.token.length()?"stored":"missing");
   Serial.printf("[config][nvs] Saving Codex: enabled=%s, access_token=%s, account_id=%s\n",cfg->codex.enabled?"yes":"no",cfg->codex.token.length()?"stored":"missing",cfg->codex.accountId.length()?"stored":"missing");
   Serial.printf("[config][nvs] Display style=%s, rows: Cursor=%u/%u/%u/%u, Codex weekly=%u\n",cfg->displayStyle==1?"open":"panels",cfg->showCursorModels,cfg->showCursorOther,cfg->showCursorOnDemand,cfg->showCursorThirtyMinute,cfg->showCodexWeekly);
   if(!saveConfig(*cfg)){server.send(500,"text/plain","Could not save settings.");return;} restartAfterResponse("Settings saved. Restarting...");
 });
 server.on("/api/ota",HTTP_POST,[]{bool ok=!Update.hasError();server.send(ok?200:500,"text/plain",ok?"Update complete; restarting":"Update failed");delay(400);if(ok)ESP.restart();},[]{HTTPUpload &u=server.upload();if(u.status==UPLOAD_FILE_START)Update.begin(UPDATE_SIZE_UNKNOWN,U_FLASH);else if(u.status==UPLOAD_FILE_WRITE)Update.write(u.buf,u.currentSize);else if(u.status==UPLOAD_FILE_END)Update.end(true);});
 server.onNotFound([](){if(isSetupMode){server.sendHeader("Location","http://192.168.4.1/",true);server.send(302,"text/plain","");}else server.send(404,"text/plain","Not found");});
 server.begin();
}
void webLoop(){server.handleClient();}
void webUpdateUsage(const UsageSnapshot &codex, const UsageSnapshot &cursor) {
  latestCodex = codex;
  latestCursor = cursor;
  hasUsageSnapshot = true;
}
