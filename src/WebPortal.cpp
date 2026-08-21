#include "WebPortal.h"
#include "Display.h"
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

static String formatClockMinutes(uint16_t minutes) {
  char value[6];
  snprintf(value, sizeof(value), "%02u:%02u", minutes / 60, minutes % 60);
  return String(value);
}

static bool parseClockMinutes(const String &value, uint16_t &minutes) {
  if (value.length() != 5 || value[2] != ':' || !isDigit(value[0]) || !isDigit(value[1]) ||
      !isDigit(value[3]) || !isDigit(value[4])) return false;
  int hour = value.substring(0, 2).toInt(), minute = value.substring(3, 5).toInt();
  if (hour > 23 || minute > 59) return false;
  minutes = hour * 60 + minute;
  return true;
}

static bool parseHtmlColor(String value, uint32_t &color) {
  value.trim();
  if (value.startsWith("#")) value.remove(0, 1);
  if (value.length() != 6) return false;
  for (uint8_t i = 0; i < 6; ++i) if (!isHexadecimalDigit(value[i])) return false;
  color = strtoul(value.c_str(), nullptr, 16) & 0xFFFFFF;
  return true;
}

static String formatHtmlColor(uint32_t color) {
  char value[8];
  snprintf(value, sizeof(value), "#%06lX", (unsigned long)(color & 0xFFFFFF));
  return String(value);
}

static const char OTA_SUCCESS_PAGE[] PROGMEM = R"HTML(
<!doctype html><html><head><meta name=viewport content="width=device-width,initial-scale=1"><title>ESP Usage update</title>
<style>:root{color-scheme:dark}body{font:16px system-ui;background:#090c11;color:#edf2f7;max-width:620px;margin:40px auto;padding:20px}main{background:#131820;border:1px solid #2a323d;border-radius:16px;padding:24px}p{color:#aeb8c4;line-height:1.55}a{display:inline-block;margin-top:12px;padding:12px 18px;border-radius:9px;background:#10a37f;color:#fff;text-decoration:none;font-weight:700}</style></head>
<body><main><h1>Firmware update complete</h1><p>ESP Usage is restarting. Wait a few seconds, then return to the web UI.</p><a href="/">Back to ESP Usage</a></main></body></html>
)HTML";

static const char OTA_FAILURE_PAGE[] PROGMEM = R"HTML(
<!doctype html><html><head><meta name=viewport content="width=device-width,initial-scale=1"><title>ESP Usage update</title>
<style>:root{color-scheme:dark}body{font:16px system-ui;background:#090c11;color:#edf2f7;max-width:620px;margin:40px auto;padding:20px}main{background:#131820;border:1px solid #2a323d;border-radius:16px;padding:24px}p{color:#f2b8b5;line-height:1.55}a{display:inline-block;margin-top:12px;padding:12px 18px;border-radius:9px;background:#263241;color:#fff;text-decoration:none;font-weight:700}</style></head>
<body><main><h1>Firmware update failed</h1><p>The firmware could not be installed. The current firmware remains active.</p><a href="/">Back to ESP Usage</a></main></body></html>
)HTML";

static const char CONFIG_SUCCESS_PAGE[] PROGMEM = R"HTML(
<!doctype html><html><head><meta name=viewport content="width=device-width,initial-scale=1"><title>ESP Usage settings</title>
<style>:root{color-scheme:dark}body{font:16px system-ui;background:#090c11;color:#edf2f7;max-width:620px;margin:40px auto;padding:20px}main{background:#131820;border:1px solid #2a323d;border-radius:16px;padding:24px}p{color:#aeb8c4;line-height:1.55}a{display:inline-block;margin-top:12px;padding:12px 18px;border-radius:9px;background:#10a37f;color:#fff;text-decoration:none;font-weight:700}</style></head>
<body><main><h1>Settings saved</h1><p>ESP Usage is restarting. Wait a few seconds, then return to the web UI.</p><a href="/">Back to ESP Usage</a></main></body></html>
)HTML";

static const char PAGE[] PROGMEM=R"HTML(
<!doctype html><html><head><meta name=viewport content="width=device-width,initial-scale=1"><title>ESP Usage</title>
<style>
  :root{color-scheme:dark}*{box-sizing:border-box}body{font:15px system-ui;background:#090c11;color:#edf2f7;max-width:1120px;margin:30px auto;padding:18px}section{background:#131820;border:1px solid #2a323d;border-radius:16px;padding:20px;margin:14px 0}h1{font-size:26px;margin-bottom:6px}h2{font-size:18px}p{color:#aeb8c4;line-height:1.5}label{display:block;color:#aeb8c4;margin:12px 0 5px}input,select{width:100%;padding:12px;border-radius:9px;border:1px solid #354153;background:#0d1117;color:#fff}input[type=checkbox]{width:auto}button{padding:12px 18px;border:0;border-radius:9px;background:#10a37f;color:#fff;font-weight:700;margin-top:16px;cursor:pointer}button.secondary{background:#263241}button.danger{background:#2a323d;color:#f2b8b5}.row{display:flex;gap:10px;align-items:end}.row>div{flex:1}.grid,.checks,.settings-grid,.utility-grid{display:grid;grid-template-columns:1fr 1fr;gap:14px}.settings-grid,.utility-grid{align-items:start}.checks{gap:12px}.checks label{margin:4px 0}.msg{min-height:20px;color:#72d7b5}.warn{color:#f4a62a}.setup .advanced{display:none}.usage-card{background:#0d1117;border:1px solid #2a323d;border-radius:11px;padding:14px}.usage-card h3{margin:0 0 8px}.usage-main{font-size:20px;color:#fff;margin:4px 0}.usage-detail{white-space:pre-line;color:#aeb8c4;line-height:1.55;min-height:92px}.mini{height:36px;display:flex;gap:5px;align-items:end;margin:12px 0 5px}.mini span{display:block;flex:1;min-height:2px;background:#35d078;border-radius:2px 2px 0 0}.usage-card.codex .mini span{background:#8295a8}.partial{color:#f4a62a}.diag{background:#0d1117;border:1px solid #2a323d;border-radius:11px;padding:14px;white-space:pre-wrap;color:#aeb8c4;line-height:1.55;overflow:auto}.ok{color:#72d7b5}.bad{color:#f4a62a}@media(max-width:760px){.grid,.checks,.settings-grid,.utility-grid{display:block}.row{display:block}.usage-card{margin:10px 0}}
</style></head><body><h1>ESP Usage</h1><p id=intro>Configure the device on your local network.</p>
<section><h2>Settings / WiFi</h2><p>Choose a detected network, enter its password, then save. Credentials are stored only in ESP32 NVS.</p>
<div class=row><div><label for=networks>Available networks</label><select id=networks><option value="">Scan for networks...</option></select></div><button class=secondary type=button onclick=scan()>Scan again</button></div>
<label for=manual>SSID (or enter a hidden network)</label><input id=manual maxlength=32 autocomplete=off>
<label for=wifi_pw>WiFi password</label><input id=wifi_pw type=password maxlength=64 autocomplete=new-password>
<button type=button onclick=saveWifi()>Save WiFi &amp; restart</button> <button class=danger type=button onclick=resetWifi()>Delete WiFi configuration</button><p class=msg id=wifiMsg></p></section>
<div class=advanced><section><h2>Live usage</h2><p>Current provider values and activity for the last 30 minutes. This view contains no access tokens or account identifiers.</p><div class=grid><div class=usage-card><h3>Cursor</h3><div class=usage-main id=cursorMain>Waiting...</div><div class=mini id=cursorChart></div><div class=usage-detail id=cursorDetail></div></div><div class="usage-card codex"><h3>Codex</h3><div class=usage-main id=codexMain>Waiting...</div><div class=mini id=codexChart></div><div class=usage-detail id=codexDetail></div></div></div></section>
  <form method=post action=/api/config><div class=settings-grid><div><section><h2>Codex</h2><p class=warn>Direct Codex limit usage is unofficial and the access token expires.</p><label><input type=checkbox name=codex_on> Enabled</label><label>Codex access_token (empty keeps current)</label><input type=password name=codex_tok autocomplete=new-password><label>ChatGPT account_id, optional (empty keeps current)</label><input type=password name=codex_acct autocomplete=off><label>Custom limit adapter URL (empty uses Codex app token)</label><input name=codex_url></section>
  <section><h2>Cursor personal</h2><p class=warn>Unofficial read-only dashboard API; may change.</p><label><input type=checkbox name=cursor_on> Enabled</label><label>Usage URL</label><input name=cursor_url value="https://cursor.com/api/usage-summary"><label>Cursor access token (empty keeps current)</label><input type=password name=cursor_tok autocomplete=new-password><p>Read from Cursor's state.vscdb key cursorAuth/accessToken.</p></section></div>
  <section><h2>Display and status</h2><label>Dashboard design</label><select name=display_style><option value=panels>Panels - framed sections</option><option value=open>Flat - open sections and larger</option></select><label>Displayed values</label><select name=display_mode><option value=used>Used</option><option value=remaining>Remaining</option></select><p>Select which usage rows are visible. The small white line on each limit bar shows how far its reset period has elapsed.</p><div class=checks><label><input type=checkbox name=show_cursor_models> Cursor Models</label><label><input type=checkbox name=show_cursor_other> Other Models</label><label><input type=checkbox name=show_cursor_ondemand> On Demand</label><label><input type=checkbox name=show_cursor_30m> Cursor 30 minutes</label><label><input type=checkbox name=show_codex_weekly> Codex weekly limit</label><label><input type=checkbox name=show_codex_30m> Codex 30 minutes</label></div><label><input type=checkbox name=display_off_enabled> Display Off Time enabled</label><div class=row><div><label>Display off from</label><input name=display_off_from type=time value="22:00"></div><div><label>Display off until</label><input name=display_off_until type=time value="07:00"></div></div><p>Uses Europe/Berlin local time. During this window, touch wakes the display for one minute.</p><label>Hostname</label><input name=host value=espusage><label>Brightness %</label><input name=bright type=number min=1 max=100 value=85><label>Refresh minutes</label><input name=refresh type=number min=1 max=1440 value=5><p>Status priority: Critical, Warning, Overpace, OK. Overpace means usage is ahead of the white pace marker; Warning and Critical override it. The same status applies in Used and Remaining mode.</p><div class=row><div><label>Overpace color</label><input name=overpace_color type=color value="#DDF542"></div><div><label>Warning color</label><input name=warning_color type=color value="#F0A020"></div></div><label>Warning from used %</label><input name=warning type=number min=1 max=98 value=70><label>Critical from used %</label><input name=critical type=number min=2 max=100 value=90><button>Save all settings &amp; restart</button></section></div></form>
  <div class=utility-grid><section><h2>Touch diagnostics</h2><p>Live GT911 and I²C data, refreshed every two seconds. Tap the display once and watch whether the counters change.</p><div class=usage-main id=touchMain>Loading...</div><pre class=diag id=touchDetail>Waiting for diagnostics...</pre></section>
  <section><h2>Firmware update</h2><form method=post action=/api/ota enctype=multipart/form-data><input type=file name=firmware accept=.bin><button>Install OTA</button></form></section></div></div>
<script>
const q=s=>document.querySelector(s), msg=t=>q('#wifiMsg').textContent=t;
q('#networks').onchange=()=>{if(q('#networks').value)q('#manual').value=q('#networks').value};
async function scan(){msg('Scanning...');q('#networks').innerHTML='<option value="">Scanning...</option>';try{let r=await fetch('/api/wifi/scan'),a=await r.json();q('#networks').innerHTML='<option value="">Select a network</option>';a.forEach(n=>{let o=document.createElement('option');o.value=n.ssid;o.textContent=`${n.secure?'🔒 ':' '}${n.ssid} (${n.rssi} dBm)`;q('#networks').append(o)});msg(a.length?`${a.length} network(s) found`:'No networks found; you can enter the SSID manually.')}catch(e){msg('Scan failed. Try again.')}}
async function saveWifi(){let ssid=q('#manual').value.trim(),password=q('#wifi_pw').value;if(!ssid){msg('Select or enter a network first.');return}msg('Saving...');let b=new URLSearchParams({ssid,password});let r=await fetch('/api/wifi',{method:'POST',body:b});msg(await r.text())}
async function resetWifi(){if(!confirm('Delete the saved WiFi configuration and start setup mode?'))return;msg('Deleting...');let r=await fetch('/api/wifi',{method:'DELETE'});msg(await r.text())}
const fmt=n=>{n=Number(n||0);return n>=1e9?(n/1e9).toFixed(2)+'B':n>=1e6?(n/1e6).toFixed(2)+'M':n>=1e3?(n/1e3).toFixed(1)+'K':String(n)};
function usageValue(w){if(w.monetary){let c=w.currency||'$',u=Number(w.used_amount||0),l=Number(w.limit_amount);return l>=0?`${c}${u.toFixed(2)} / ${c}${l.toFixed(2)}`:`${c}${u.toFixed(2)}`}return w.used_percent<0?'--':w.used_percent.toFixed(1)+'%'}
function chart(id,buckets,field){let root=q(id),max=Math.max(0,...buckets.map(b=>b.valid?Number(b[field]||0):0));root.replaceChildren();buckets.forEach(b=>{let bar=document.createElement('span'),v=Number(b[field]||0);bar.style.height=(b.valid&&max?Math.max(2,Math.round(34*v/max)):2)+'px';bar.style.opacity=b.valid?'1':'.25';bar.title=b.valid?String(v):'No sample';root.append(bar)})}
function renderCursor(c){let r=c.recent_30m||{},tokens=Number(r.total_tokens||0),partial=r.tokenized_calls<r.calls||r.partial;q('#cursorMain').textContent=!r.available?'NO DATA':r.calls?(r.token_data?`${fmt(tokens)}${partial?'+':''} TOK | ${r.calls} CALLS`:`${r.calls} CALLS | TOK N/A`):'0 CALLS';q('#cursorDetail').textContent=`Models ${c.primary.used_percent<0?'--':c.primary.used_percent.toFixed(1)}% used | Other ${c.secondary.used_percent<0?'--':c.secondary.used_percent.toFixed(1)}% | On Demand ${usageValue(c.tertiary)}\nInput ${fmt(r.input_tokens)} | Output ${fmt(r.output_tokens)}\nCache read ${fmt(r.cache_read_tokens)} | Cache write ${fmt(r.cache_write_tokens)}\nTop model ${r.top_model||'n/a'} | Type ${r.top_kind||'n/a'} | Max Mode ${r.max_mode_calls||0}${r.cost_cents>=0?' | Cost $'+(r.cost_cents/100).toFixed(4):''}\n${partial?'Partial token coverage: some calls did not report tokens.':(r.status||c.status)}`;q('#cursorDetail').classList.toggle('partial',partial);chart('#cursorChart',r.buckets||[],r.token_data&&tokens>0?'tokens':'calls')}
function renderCodex(c){let w=c.secondary||{},r=c.recent_30m||{},valid=(r.buckets||[]).filter(b=>b.valid).length;q('#codexMain').textContent=!r.available?'NO DATA':r.ready?`+${Number(r.delta_pp||0).toFixed(2)} PP`:`COLLECTING 1/2`;q('#codexDetail').textContent=`Weekly ${w.used_percent<0?'--':w.used_percent.toFixed(1)}% used\n${w.reset||'Reset time unavailable'}\nMeasurements ${r.samples||0} | Filled 5-minute buckets ${valid}/6\n${r.status||c.status}`;chart('#codexChart',r.buckets||[],'delta_pp')}
async function loadUsage(){try{let r=await fetch('/api/usage'),u=await r.json();if(u.ready){renderCursor(u.cursor);renderCodex(u.codex)}}catch(e){q('#cursorMain').textContent=q('#codexMain').textContent='Unavailable'}}
const hx=n=>'0x'+Number(n||0).toString(16).toUpperCase().padStart(2,'0');
async function loadTouch(){try{let r=await fetch('/api/touch',{cache:'no-store'}),t=await r.json(),working=t.point_frames>0||t.tap_events>0;q('#touchMain').textContent=t.status;q('#touchMain').className='usage-main '+(working?'ok':'bad');q('#touchDetail').textContent=`Controller: ${t.controller}${t.address?' @ '+hx(t.address):''}\nI2C: SDA ${t.sda}, SCL ${t.scl}, 50 kHz | devices: ${t.bus_devices}\nDisplay: RGB 480 x 480 | pixel clock: 10 MHz\nRaw size: ${t.raw_width} x ${t.raw_height}\nLVGL callbacks: ${t.callback_calls} | fallback reads: ${t.fallback_reads} | GT911 polls: ${t.polls}\nState reads: ${t.state_reads} | errors: ${t.state_read_errors} | last state: ${hx(t.last_state)} | touches: ${t.last_touch_count}\nReady frames: ${t.ready_frames} | point frames: ${t.point_frames} | ACK errors: ${t.ack_errors}\nLast point raw: ${t.raw_x}, ${t.raw_y} | display: ${t.display_x}, ${t.display_y} | pressed: ${t.pressed}\nGestures down/up/tap/toggle: ${t.down_events}/${t.up_events}/${t.tap_events}/${t.toggle_events}\nLast event: ${t.last_event} at ${t.last_event_ms} ms\nLast error: ${t.last_error}\nPossible GSL3680 at 0x40: ${t.possible_gsl3680?'yes':'no'} | view: ${t.remaining_view?'remaining':'used'}`;}catch(e){q('#touchMain').textContent='Touch diagnostics unavailable';q('#touchMain').className='usage-main bad'}}
  fetch('/api/status').then(r=>r.json()).then(s=>{if(s.setup_mode){document.body.classList.add('setup');q('#intro').textContent='Setup mode: choose your WiFi network to continue.'}else if(s.ssid){q('#manual').value=s.ssid}q('[name=codex_on]').checked=!!s.codex_configured;q('[name=cursor_on]').checked=!!s.cursor_configured;q('[name=codex_url]').value=s.codex_endpoint||'';q('[name=cursor_url]').value=s.cursor_endpoint||'https://cursor.com/api/usage-summary';q('[name=display_style]').value=s.display_style||'panels';q('[name=display_mode]').value=s.display_mode||'used';q('[name=show_cursor_models]').checked=!!s.show_cursor_models;q('[name=show_cursor_other]').checked=!!s.show_cursor_other;q('[name=show_cursor_ondemand]').checked=!!s.show_cursor_ondemand;q('[name=show_cursor_30m]').checked=!!s.show_cursor_30m;q('[name=show_codex_weekly]').checked=!!s.show_codex_weekly;q('[name=show_codex_30m]').checked=!!s.show_codex_30m;q('[name=display_off_enabled]').checked=!!s.display_off_enabled;q('[name=display_off_from]').value=s.display_off_from||'22:00';q('[name=display_off_until]').value=s.display_off_until||'07:00';q('[name=host]').value=s.hostname||'espusage';q('[name=bright]').value=s.brightness||85;q('[name=refresh]').value=s.refresh_minutes||5;q('[name=overpace_color]').value=s.overpace_color||'#DDF542';q('[name=warning_color]').value=s.warning_color||'#F0A020';q('[name=warning]').value=s.warning_percent||70;q('[name=critical]').value=s.critical_percent||90});scan();loadUsage();setInterval(loadUsage,15000);
loadTouch();setInterval(loadTouch,2000);
</script></body></html>)HTML";

static void addWindowJson(JsonObject target, const UsageWindow &window) {
  target["label"] = window.label;
  target["used_percent"] = window.usedPercent;
  target["elapsed_percent"] = window.elapsedPercent;
  target["monetary"] = window.monetary;
  target["used_amount"] = window.usedAmount;
  target["limit_amount"] = window.limitAmount;
  target["currency"] = window.currencySymbol;
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
   d["display_mode"]=cfg->displayAvailable?"remaining":"used";
   d["show_cursor_models"]=cfg->showCursorModels; d["show_cursor_other"]=cfg->showCursorOther; d["show_cursor_ondemand"]=cfg->showCursorOnDemand;
   d["show_cursor_30m"]=cfg->showCursorThirtyMinute; d["show_codex_weekly"]=cfg->showCodexWeekly;
   d["show_codex_30m"]=cfg->showCodexThirtyMinute;
   d["display_off_enabled"]=cfg->displayOffEnabled;
   d["display_off_from"]=formatClockMinutes(cfg->displayOffFromMinutes);
   d["display_off_until"]=formatClockMinutes(cfg->displayOffUntilMinutes);
   d["brightness"]=cfg->brightness; d["refresh_minutes"]=cfg->refreshMinutes;
   d["warning_percent"]=cfg->warningPercent; d["critical_percent"]=cfg->criticalPercent;
   d["overpace_color"]=formatHtmlColor(cfg->overpaceColor); d["warning_color"]=formatHtmlColor(cfg->warningColor);
   String s; serializeJson(d,s); server.send(200,"application/json",s);
 });
 server.on("/api/usage",HTTP_GET,[]{
   JsonDocument d; d["ready"]=hasUsageSnapshot;
   addSnapshotJson(d["cursor"].to<JsonObject>(), latestCursor);
   addSnapshotJson(d["codex"].to<JsonObject>(), latestCodex);
   String s; serializeJson(d,s); server.send(200,"application/json",s);
 });
 server.on("/api/touch",HTTP_GET,[]{
   TouchDiagnostics t=displayGetTouchDiagnostics(); JsonDocument d;
   d["status"]=t.status; d["controller"]=t.controller; d["address"]=t.address;
   d["sda"]=19; d["scl"]=45; d["bus_devices"]=t.busDevices; d["possible_gsl3680"]=t.possibleGsl3680;
   d["raw_width"]=t.rawWidth; d["raw_height"]=t.rawHeight; d["raw_x"]=t.rawX; d["raw_y"]=t.rawY;
   d["display_x"]=t.displayX; d["display_y"]=t.displayY; d["pressed"]=t.pressed;
   d["callback_calls"]=t.callbackCalls; d["fallback_reads"]=t.fallbackReads; d["probe_attempts"]=t.probeAttempts; d["polls"]=t.polls;
   d["state_reads"]=t.stateReads; d["state_read_errors"]=t.stateReadErrors;
   d["last_state"]=t.lastState; d["last_touch_count"]=t.lastTouchCount;
   d["ready_frames"]=t.readyFrames; d["point_frames"]=t.pointFrames; d["ack_errors"]=t.acknowledgeErrors;
   d["down_events"]=t.downEvents; d["up_events"]=t.upEvents; d["tap_events"]=t.tapEvents; d["toggle_events"]=t.toggleEvents;
   d["last_event"]=t.lastEvent; d["last_event_ms"]=t.lastEventMs; d["last_error"]=t.lastError;
   d["remaining_view"]=t.remainingView;
   String s; serializeJson(d,s); server.sendHeader("Cache-Control","no-store"); server.send(200,"application/json",s);
 });
 server.on("/api/display",HTTP_GET,[]{
   String mode=server.arg("mode"); mode.toLowerCase();
   bool remaining;
   if(mode=="toggle") remaining=displayToggleRemainingView();
   else if(mode=="used") remaining=displaySetRemainingView(false);
   else if(mode=="remaining") remaining=displaySetRemainingView(true);
   else {
     server.sendHeader("Cache-Control","no-store");
     server.send(400,"application/json","{\"ok\":false,\"error\":\"mode must be toggle, used, or remaining\"}");
     return;
   }
   JsonDocument d; d["ok"]=true; d["mode"]=remaining?"remaining":"used";
   String s; serializeJson(d,s); server.sendHeader("Cache-Control","no-store"); server.send(200,"application/json",s);
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
   cfg->displayStyle=server.arg("display_style")=="open"?1:0; cfg->displayAvailable=server.arg("display_mode")=="remaining";
   cfg->showCursorModels=server.hasArg("show_cursor_models"); cfg->showCursorOther=server.hasArg("show_cursor_other");
   cfg->showCursorOnDemand=server.hasArg("show_cursor_ondemand"); cfg->showCursorThirtyMinute=server.hasArg("show_cursor_30m");
   cfg->showCodexWeekly=server.hasArg("show_codex_weekly"); cfg->showCodexThirtyMinute=server.hasArg("show_codex_30m");
   uint16_t offFrom=cfg->displayOffFromMinutes,offUntil=cfg->displayOffUntilMinutes;
   bool validFrom=parseClockMinutes(server.arg("display_off_from"),offFrom),validUntil=parseClockMinutes(server.arg("display_off_until"),offUntil);
   cfg->displayOffEnabled=server.hasArg("display_off_enabled")&&validFrom&&validUntil&&offFrom!=offUntil;
   cfg->displayOffFromMinutes=offFrom; cfg->displayOffUntilMinutes=offUntil;
   cfg->brightness=constrain(server.arg("bright").toInt(),1,100); cfg->refreshMinutes=constrain(server.arg("refresh").toInt(),1,1440);
   cfg->warningPercent=constrain(server.arg("warning").toInt(),1,98); cfg->criticalPercent=constrain(server.arg("critical").toInt(),cfg->warningPercent+1,100);
   parseHtmlColor(server.arg("overpace_color"),cfg->overpaceColor); parseHtmlColor(server.arg("warning_color"),cfg->warningColor);
   Serial.printf("[config][nvs] Saving Cursor: enabled=%s, token=%s\n",cfg->cursor.enabled?"yes":"no",cfg->cursor.token.length()?"stored":"missing");
   Serial.printf("[config][nvs] Saving Codex: enabled=%s, access_token=%s, account_id=%s\n",cfg->codex.enabled?"yes":"no",cfg->codex.token.length()?"stored":"missing",cfg->codex.accountId.length()?"stored":"missing");
   Serial.printf("[config][nvs] Display style=%s, mode=%s, rows: Cursor=%u/%u/%u/%u, Codex=%u/%u\n",cfg->displayStyle==1?"open":"panels",cfg->displayAvailable?"remaining":"used",cfg->showCursorModels,cfg->showCursorOther,cfg->showCursorOnDemand,cfg->showCursorThirtyMinute,cfg->showCodexWeekly,cfg->showCodexThirtyMinute);
   Serial.printf("[config][nvs] Display off time: %s, %02u:%02u-%02u:%02u Europe/Berlin\n",cfg->displayOffEnabled?"enabled":"disabled",cfg->displayOffFromMinutes/60,cfg->displayOffFromMinutes%60,cfg->displayOffUntilMinutes/60,cfg->displayOffUntilMinutes%60);
   if(!saveConfig(*cfg)){server.send(500,"text/plain","Could not save settings.");return;}
   server.send_P(200,"text/html",CONFIG_SUCCESS_PAGE); delay(750); ESP.restart();
 });
 server.on("/api/ota",HTTP_POST,[]{
   bool ok=!Update.hasError();
   server.send_P(ok?200:500,"text/html",ok?OTA_SUCCESS_PAGE:OTA_FAILURE_PAGE);
   delay(750);
   if(ok)ESP.restart();
 },[]{HTTPUpload &u=server.upload();if(u.status==UPLOAD_FILE_START)Update.begin(UPDATE_SIZE_UNKNOWN,U_FLASH);else if(u.status==UPLOAD_FILE_WRITE)Update.write(u.buf,u.currentSize);else if(u.status==UPLOAD_FILE_END)Update.end(true);});
 server.onNotFound([](){if(isSetupMode){server.sendHeader("Location","http://192.168.4.1/",true);server.send(302,"text/plain","");}else server.send(404,"text/plain","Not found");});
 server.begin();
}
void webLoop(){server.handleClient();}
void webUpdateUsage(const UsageSnapshot &codex, const UsageSnapshot &cursor) {
  latestCodex = codex;
  latestCursor = cursor;
  hasUsageSnapshot = true;
}

