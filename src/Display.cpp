#include "Display.h"
#include "ProviderIcons.h"
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <lvgl.h>

static Arduino_DataBus *bus = new Arduino_SWSPI(GFX_NOT_DEFINED, 39, 48, 47, GFX_NOT_DEFINED);
static Arduino_ESP32RGBPanel *rgb = new Arduino_ESP32RGBPanel(18,17,16,21,11,12,13,14,0,8,20,3,46,9,10,4,5,6,7,15,1,10,8,50,1,10,8,20,0,10000000,false,0,0,0);
static Arduino_RGB_Display *gfx = new Arduino_RGB_Display(480,480,rgb,0,true,bus,GFX_NOT_DEFINED,st7701_type9_init_operations,sizeof(st7701_type9_init_operations));
static lv_disp_draw_buf_t drawBuf;
static lv_color_t *drawMemory;

static constexpr uint8_t TOUCH_SDA = 19;
static constexpr uint8_t TOUCH_SCL = 45;
static constexpr uint8_t GT911_PRIMARY_ADDRESS = 0x5D;
static constexpr uint8_t GT911_SECONDARY_ADDRESS = 0x14;
static constexpr uint16_t GT911_SWITCHES = 0x804D;
static constexpr uint16_t GT911_MAX_VALUES = 0x8048;
static constexpr uint16_t GT911_TOUCH_STATE = 0x814E;
static constexpr uint16_t GT911_FIRST_POINT = 0x814F;
static uint8_t touchAddress = 0;
static uint32_t touchLastPollMs = 0, touchLastFrameMs = 0, touchLastProbeMs = 0;
static uint8_t touchReadErrors = 0;
static bool touchPressed = false;
static int touchX = 0, touchY = 0;
static bool touchWasPressed = false, touchPendingToggle = false, detailsOpen = false;
static uint32_t touchDownMs = 0;
static int touchDownX = 0, touchDownY = 0;
static String touchBusDevices = "not scanned", touchLastError = "not initialized", touchLastEvent = "none";
static bool touchPossibleGsl3680 = false;
static uint8_t touchLastState = 0, touchLastCount = 0;
static uint16_t touchRawWidth = 0, touchRawHeight = 0, touchRawX = 0, touchRawY = 0;
static uint32_t touchCallbackCalls = 0, touchProbeAttempts = 0, touchPolls = 0, touchStateReads = 0;
static uint32_t touchStateReadErrors = 0, touchReadyFrames = 0, touchPointFrames = 0, touchAcknowledgeErrors = 0;
static uint32_t touchDownEvents = 0, touchUpEvents = 0, touchTapEvents = 0, touchToggleEvents = 0, touchLastEventMs = 0;
static uint32_t touchLastCallbackMs = 0, touchFallbackReads = 0;
static lv_indev_t *touchInputDevice = nullptr;

static lv_obj_t *networkLabel, *modeLabel, *providerStatusLabels[2];
static lv_obj_t *statusLabels[4], *values[4], *bars[4], *paceMarkers[4], *resetLabels[4], *rows[4];
static lv_obj_t *paceRows[2], *paceValues[2], *paceMeta[2], *paceColumns[2][6];
static int barWidths[4], markerY[4], paceChartBaseline[2], paceChartMaxHeight[2];
static String rowNames[4] = {"CURSOR MODELS","OTHER MODELS","ON DEMAND","WEEKLY LIMIT"};
static UsageWindow rowData[4];
static String rowStatus[4], networkAddress;
static UsageSnapshot latestCodex, latestCursor;
static bool availableView = false;
static uint8_t warningLevel = 70, criticalLevel = 90;

static lv_color_t C(uint32_t value) { return lv_color_hex(value); }

static lv_obj_t *label(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color) {
  lv_obj_t *object = lv_label_create(parent);
  lv_label_set_text(object, text);
  lv_obj_set_style_text_font(object, font, 0);
  lv_obj_set_style_text_color(object, color, 0);
  return object;
}

static void flush(lv_disp_drv_t *driver, const lv_area_t *area, lv_color_t *pixels) {
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)&pixels->full,
                          area->x2 - area->x1 + 1, area->y2 - area->y1 + 1);
  lv_disp_flush_ready(driver);
}

static bool touchReadRegister(uint16_t reg, uint8_t *data, size_t length) {
  if (!touchAddress) return false;
  Wire.beginTransmission(touchAddress);
  Wire.write((uint8_t)(reg >> 8));
  Wire.write((uint8_t)(reg & 0xFF));
  // ESPHome's working GT911 driver finishes the register-pointer write before
  // starting the read. Some 4848S040 revisions do not answer reliably to the
  // repeated-start transaction previously used here.
  if (Wire.endTransmission(true) != 0) return false;
  if (Wire.requestFrom(touchAddress, (uint8_t)length, (uint8_t)true) != length) return false;
  for (size_t i = 0; i < length; ++i) data[i] = Wire.read();
  return true;
}

static bool touchWriteRegister(uint16_t reg, uint8_t value) {
  if (!touchAddress) return false;
  Wire.beginTransmission(touchAddress);
  Wire.write((uint8_t)(reg >> 8));
  Wire.write((uint8_t)(reg & 0xFF));
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

static void touchScanBus() {
  String found;
  touchPossibleGsl3680 = false;
  for (uint8_t address = 1; address < 127; ++address) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission(true) == 0) {
      if (address == 0x40) touchPossibleGsl3680 = true;
      if (found.length()) found += ", ";
      char item[8]; snprintf(item, sizeof(item), "0x%02X", address); found += item;
    }
  }
  touchBusDevices = found.length() ? found : "none";
  Serial.printf("[touch][i2c] Bus scan on SDA=%u/SCL=%u: %s\n", TOUCH_SDA, TOUCH_SCL,
                touchBusDevices.c_str());
}

static bool touchProbe() {
  touchProbeAttempts++;
  uint8_t switches = 0;
  const uint8_t addresses[] = {GT911_PRIMARY_ADDRESS, GT911_SECONDARY_ADDRESS};
  for (uint8_t address : addresses) {
    touchAddress = address;
    if (!touchReadRegister(GT911_SWITCHES, &switches, 1)) continue;
    uint8_t limits[4] = {};
    uint16_t rawWidth = 0, rawHeight = 0;
    if (touchReadRegister(GT911_MAX_VALUES, limits, sizeof(limits))) {
      rawWidth = (uint16_t)limits[0] | ((uint16_t)limits[1] << 8);
      rawHeight = (uint16_t)limits[2] | ((uint16_t)limits[3] << 8);
    }
    touchRawWidth = rawWidth; touchRawHeight = rawHeight;
    touchReadErrors = 0;
    touchLastError = "none";
    Serial.printf("[touch][gt911] Detected at 0x%02X, raw=%ux%u, poll=16ms, transform=180deg\n",
                  touchAddress, rawWidth, rawHeight);
    return true;
  }
  touchAddress = 0;
  touchLastError = touchPossibleGsl3680
                     ? "GT911 missing; I2C device 0x40 may be a GSL3680 variant"
                     : "GT911 not found at 0x5D or 0x14";
  Serial.println("[touch][gt911] Not found at 0x5D or 0x14; retrying in 2s");
  return false;
}

static void touchBegin() {
  // EspControl leaves reset/interrupt unassigned on the 4848S040. GPIO41/42
  // are SD-card pins on this board and must never be toggled for touch setup.
  pinMode(TOUCH_SDA, INPUT_PULLUP);
  pinMode(TOUCH_SCL, INPUT_PULLUP);
  Wire.begin(TOUCH_SDA, TOUCH_SCL);
  Wire.setClock(50000);  // EspControl/ESPHome default for this exact board.
  Wire.setTimeOut(50);
  touchLastProbeMs = millis();
  touchScanBus();
  touchProbe();
  Serial.println("[touch][gesture] Raw tap detection enabled (35-650ms)");
}

static void readTouch(lv_indev_drv_t *, lv_indev_data_t *data) {
  touchCallbackCalls++;
  uint32_t now = millis();
  touchLastCallbackMs = now;
  if (!touchAddress && now - touchLastProbeMs >= 2000) {
    touchLastProbeMs = now;
    touchProbe();
  }
  if (touchAddress && now - touchLastPollMs >= 16) {
    touchLastPollMs = now;
    touchPolls++;
    uint8_t state = 0;
    if (!touchReadRegister(GT911_TOUCH_STATE, &state, 1)) {
      touchStateReadErrors++;
      touchLastError = "GT911 touch-state read failed";
      if (++touchReadErrors == 8) {
        Serial.printf("[touch][gt911] Repeated I2C errors at 0x%02X; probing both addresses again\n", touchAddress);
        touchAddress = 0;
        touchPressed = false;
        touchLastProbeMs = now;
      }
    } else {
      touchStateReads++;
      touchReadErrors = 0;
      uint8_t count = state & 0x07;
      touchLastState = state; touchLastCount = count;
      if (state & 0x80) {
        touchReadyFrames++;
        // Match the proven ESPHome GT911 sequence used by EspControl: clear
        // the ready flag immediately, then fetch the current point buffer.
        if (!touchWriteRegister(GT911_TOUCH_STATE, 0)) {
          touchAcknowledgeErrors++;
          touchLastError = "GT911 frame acknowledgement failed";
          Serial.println("[touch][gt911] Could not acknowledge touch frame");
        }
        touchLastFrameMs = now;
        if (count > 0 && count <= 5) {
          uint8_t points[41] = {};
          uint8_t readLength = count * 8 + 1;  // all touches plus key byte
          if (touchReadRegister(GT911_FIRST_POINT, points, readLength)) {
            uint16_t rawX = (uint16_t)points[1] | ((uint16_t)points[2] << 8);
            uint16_t rawY = (uint16_t)points[3] | ((uint16_t)points[4] << 8);
            touchRawX = rawX; touchRawY = rawY; touchPointFrames++;
            touchLastError = "none";
            // EspControl uses mirror_x/y=false and LVGL rotation=180. Our LVGL
            // framebuffer is unrotated, so apply that single 180° transform here.
            touchX = constrain(479 - (int)rawX, 0, 479);
            touchY = constrain(479 - (int)rawY, 0, 479);
            touchPressed = true;
          } else touchLastError = "GT911 point-buffer read failed";
        } else touchPressed = false;
      } else if (touchPressed && now - touchLastFrameMs > 120) touchPressed = false;
    }
  }
  data->state = touchPressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
  data->point.x = touchX;
  data->point.y = touchY;
  if (touchPressed && !touchWasPressed) {
    touchDownMs = now; touchDownX = touchX; touchDownY = touchY;
    touchDownEvents++; touchLastEventMs = now; touchLastEvent = "down";
    Serial.printf("[touch][gesture] down x=%d y=%d\n", touchX, touchY);
  }
  if (!touchPressed && touchWasPressed) {
    uint32_t duration = now - touchDownMs;
    int movement = abs(touchX - touchDownX) + abs(touchY - touchDownY);
    bool tap = duration >= 35 && duration <= 650 && movement <= 80;
    touchUpEvents++; touchLastEventMs = now; touchLastEvent = "up";
    if (tap && !detailsOpen) {
      touchPendingToggle = true; touchTapEvents++; touchLastEvent = "tap queued";
    }
    Serial.printf("[touch][gesture] up x=%d y=%d, duration=%lums, movement=%d -> %s\n",
                  touchX, touchY, (unsigned long)duration, movement,
                  tap ? (detailsOpen ? "ignored (details open)" : "toggle queued") : "no tap");
  }
  touchWasPressed = touchPressed;
}

static void panelStyle(lv_obj_t *object) {
  lv_obj_set_style_bg_color(object, C(0x101010), 0);
  lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(object, 1, 0);
  lv_obj_set_style_border_color(object, C(0x303030), 0);
  lv_obj_set_style_radius(object, 12, 0);
  lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
}

static uint64_t totalTokens(const RecentUsage30m &recent) {
  return recent.inputTokens + recent.outputTokens + recent.cacheWriteTokens + recent.cacheReadTokens;
}

static String formatUnsigned(uint64_t value) {
  char buffer[24];
  snprintf(buffer, sizeof(buffer), "%llu", (unsigned long long)value);
  return String(buffer);
}

static String formatTokens(uint64_t value) {
  if (value >= 1000000000ULL) return String((double)value / 1000000000.0, 2) + "B";
  if (value >= 1000000ULL) return String((double)value / 1000000.0, 2) + "M";
  if (value >= 1000ULL) return String((double)value / 1000.0, 1) + "K";
  return formatUnsigned(value);
}

static lv_color_t usageColor(float usedPercent) {
  if (usedPercent < 0) return C(0x7D7D7D);
  if (usedPercent >= criticalLevel) return C(0xFF5050);
  if (usedPercent >= warningLevel) return C(0xF0A020);
  return C(0x35D078);
}

static String usageState(float usedPercent) {
  if (usedPercent < 0) return "NO DATA";
  if (!availableView) {
    if (usedPercent >= criticalLevel) return "CRITICAL";
    if (usedPercent >= warningLevel) return "WARNING";
    return "OK";
  }
  float available = 100.0f - usedPercent;
  if (available <= 100 - criticalLevel) return "LOW";
  if (available <= 100 - warningLevel) return "WATCH";
  return "OK";
}

static uint8_t validBucketCount(const RecentUsage30m &recent) {
  uint8_t count = 0;
  for (const RecentUsageBucket &bucket : recent.buckets) if (bucket.valid) count++;
  return count;
}

static void closeModal(lv_event_t *event) {
  lv_obj_t *backdrop = (lv_obj_t *)lv_event_get_user_data(event);
  Serial.println("[touch] Details closed");
  detailsOpen = false;
  lv_obj_del(backdrop);
}

static String standardDetails(uint8_t index) {
  const UsageWindow &window = rowData[index];
  String body;
  if (window.usedPercent < 0) body = "No usage data";
  else {
    body = "Used: " + String(window.usedPercent, 1) + "%\nRemaining: " + String(100.0f - window.usedPercent, 1) + "%";
    if (window.elapsedPercent >= 0) {
      body += "\nPeriod elapsed: " + String(window.elapsedPercent, 1) + "%";
      body += "\nPeriod remaining: " + String(100.0f - window.elapsedPercent, 1) + "%";
    }
  }
  body += "\n\n" + (window.resetText.length() ? window.resetText : String("Reset unavailable"));
  body += "\nProvider: " + rowStatus[index];
  return body;
}

static String cursorPaceDetails() {
  const RecentUsage30m &recent = latestCursor.recent30m;
  String body = "Calls: " + String(recent.calls) + "\nToken data: " + String(recent.tokenizedCalls) + "/" + String(recent.calls) + " calls";
  body += "\nTotal tokens: " + formatUnsigned(totalTokens(recent));
  body += "\nInput: " + formatUnsigned(recent.inputTokens) + "\nOutput: " + formatUnsigned(recent.outputTokens);
  body += "\nCache read: " + formatUnsigned(recent.cacheReadTokens) + "\nCache write: " + formatUnsigned(recent.cacheWriteTokens);
  if (recent.costCents >= 0) body += "\nCost: $" + String(recent.costCents / 100.0f, 4);
  if (recent.topModel.length()) body += "\nTop model: " + recent.topModel;
  if (recent.topKind.length()) body += "\nTop call type: " + recent.topKind;
  body += "\nMax Mode calls: " + String(recent.maxModeCalls);
  body += "\n\nStatus: " + (recent.status.length() ? recent.status : String("unavailable"));
  return body;
}

static String codexPaceDetails() {
  const RecentUsage30m &recent = latestCodex.recent30m;
  String body = "Weekly change: +" + String(recent.deltaPercent, 2) + " pp\nMeasurements: " + String(recent.samples) +
                "\nFilled buckets: " + String(validBucketCount(recent)) + "/6\n\n5-minute buckets:";
  for (uint8_t i = 0; i < 6; ++i) {
    body += "\n" + String(i + 1) + ": ";
    body += recent.buckets[i].valid ? "+" + String(recent.buckets[i].deltaPercent, 2) + " pp" : String("no sample");
  }
  body += "\n\nStatus: " + (recent.status.length() ? recent.status : String("unavailable"));
  return body;
}

static void openDetails(lv_event_t *event) {
  if (detailsOpen) return;
  detailsOpen = true;
  uint8_t index = (uint8_t)(uintptr_t)lv_event_get_user_data(event);
  Serial.printf("[touch] long press -> details=%u\n", index);
  lv_obj_t *backdrop = lv_obj_create(lv_scr_act());
  lv_obj_set_size(backdrop, 480, 480); lv_obj_set_pos(backdrop, 0, 0);
  lv_obj_set_style_bg_color(backdrop, C(0x000000), 0); lv_obj_set_style_bg_opa(backdrop, LV_OPA_80, 0);
  lv_obj_set_style_border_width(backdrop, 0, 0); lv_obj_set_style_pad_all(backdrop, 0, 0);
  lv_obj_clear_flag(backdrop, LV_OBJ_FLAG_SCROLLABLE); lv_obj_add_flag(backdrop, LV_OBJ_FLAG_CLICKABLE);

  lv_obj_t *modal = lv_obj_create(backdrop);
  lv_obj_set_size(modal, 438, index < 4 ? 330 : 410); lv_obj_center(modal); panelStyle(modal);
  lv_obj_set_style_pad_all(modal, 20, 0);
  const char *titleText = index < 4 ? rowNames[index].c_str() : index == 4 ? "CURSOR / LAST 30 MIN" : "CODEX / LAST 30 MIN";
  lv_obj_t *title = label(modal, titleText, &lv_font_montserrat_20, C(0xF4F4F4));
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);
  String bodyText = index < 4 ? standardDetails(index) : index == 4 ? cursorPaceDetails() : codexPaceDetails();
  lv_obj_t *body = label(modal, bodyText.c_str(), &lv_font_montserrat_14, C(0xB8B8B8));
  lv_obj_set_width(body, 390); lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP); lv_obj_align(body, LV_ALIGN_TOP_LEFT, 0, 42);
  lv_obj_t *close = lv_btn_create(modal);
  lv_obj_set_size(close, 104, 40); lv_obj_align(close, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
  lv_obj_set_style_bg_color(close, C(0x252525), 0); lv_obj_set_style_radius(close, 8, 0);
  lv_obj_t *closeText = label(close, "CLOSE", &lv_font_montserrat_14, C(0xFFFFFF)); lv_obj_center(closeText);
  lv_obj_add_event_cb(close, closeModal, LV_EVENT_SHORT_CLICKED, backdrop);
}

static void renderMetric(uint8_t index) {
  if (!rows[index]) return;
  const UsageWindow &window = rowData[index];
  float used = window.usedPercent;
  float shown = used < 0 ? -1 : availableView ? 100.0f - used : used;
  lv_color_t color = usageColor(used);
  String valueText = shown < 0 ? "--%" : String(shown, 0) + "%";
  String state = usageState(used);
  lv_label_set_text(statusLabels[index], state.c_str()); lv_obj_set_style_text_color(statusLabels[index], color, 0);
  lv_label_set_text(values[index], valueText.c_str()); lv_obj_set_style_text_color(values[index], color, 0);
  lv_obj_set_style_base_dir(bars[index], availableView ? LV_BASE_DIR_RTL : LV_BASE_DIR_LTR, 0);
  lv_obj_set_style_bg_color(bars[index], color, LV_PART_INDICATOR);
  lv_bar_set_value(bars[index], shown < 0 ? 0 : (int)constrain(shown, 0, 100), LV_ANIM_OFF);

  if (window.elapsedPercent >= 0) {
    float elapsed = constrain(window.elapsedPercent, 0.0f, 100.0f);
    float remaining = 100.0f - elapsed;
    int markerX = availableView
      ? (barWidths[index] - 3) - (int)(remaining * (barWidths[index] - 3) / 100.0f)
      : (int)(elapsed * (barWidths[index] - 3) / 100.0f);
    lv_obj_set_pos(paceMarkers[index], markerX, markerY[index]);
    lv_obj_clear_flag(paceMarkers[index], LV_OBJ_FLAG_HIDDEN); lv_obj_move_foreground(paceMarkers[index]);
  } else lv_obj_add_flag(paceMarkers[index], LV_OBJ_FLAG_HIDDEN);

  String bottom = window.resetText.length() ? window.resetText : rowStatus[index];
  if (used < 0 && networkAddress.length() && (rowStatus[index] == "disabled" || rowStatus[index].indexOf("missing") >= 0))
    bottom = "Setup: http://" + networkAddress;
  lv_label_set_text(resetLabels[index], bottom.c_str());
}

static void renderPace(uint8_t provider) {
  if (!paceRows[provider]) return;
  const RecentUsage30m &recent = provider == 0 ? latestCursor.recent30m : latestCodex.recent30m;
  String valueText, metaText;
  bool tokenChart = false;
  if (provider == 0) {
    uint64_t tokens = totalTokens(recent);
    tokenChart = recent.tokenData && tokens > 0;
    if (!recent.available) valueText = "NO DATA";
    else if (!recent.calls) valueText = "0 CALLS";
    else if (recent.tokenData) valueText = formatTokens(tokens) + (recent.tokenizedCalls < recent.calls ? "+ TOK | " : " TOK | ") + String(recent.calls) + " CALLS";
    else valueText = String(recent.calls) + " CALLS | TOK N/A";
    metaText = recent.topModel.length() ? recent.topModel : tokenChart ? "TOKENS / 6 x 5 MIN" : "CALLS / 6 x 5 MIN";
  } else {
    if (!recent.available) valueText = "NO DATA";
    else if (!recent.ready) valueText = "COLLECTING 1/2";
    else valueText = "+" + String(recent.deltaPercent, 2) + " PP";
    metaText = "WEEKLY CHANGE / 6 x 5 MIN";
  }
  lv_label_set_text(paceValues[provider], valueText.c_str());
  lv_label_set_text(paceMeta[provider], metaText.c_str());

  double maximum = 0;
  for (uint8_t i = 0; i < 6; ++i) {
    double amount = provider == 0 ? (tokenChart ? (double)recent.buckets[i].tokens : recent.buckets[i].calls) : recent.buckets[i].deltaPercent;
    if (recent.buckets[i].valid && amount > maximum) maximum = amount;
  }
  for (uint8_t i = 0; i < 6; ++i) {
    double amount = provider == 0 ? (tokenChart ? (double)recent.buckets[i].tokens : recent.buckets[i].calls) : recent.buckets[i].deltaPercent;
    int height = !recent.buckets[i].valid ? 2 : maximum <= 0 || amount <= 0 ? 2 : 2 + (int)(paceChartMaxHeight[provider] * amount / maximum);
    lv_obj_set_height(paceColumns[provider][i], height);
    lv_obj_set_y(paceColumns[provider][i], paceChartBaseline[provider] - height);
    lv_obj_set_style_bg_opa(paceColumns[provider][i], recent.buckets[i].valid ? LV_OPA_COVER : LV_OPA_20, 0);
  }
}

static void renderAll() {
  lv_label_set_text(modeLabel, availableView ? "REMAINING" : "USED");
  for (uint8_t i = 0; i < 4; ++i) renderMetric(i);
  renderPace(0); renderPace(1);
  lv_obj_invalidate(lv_scr_act()); lv_refr_now(nullptr);
}

static void toggleView() {
  availableView = !availableView;
  Serial.printf("[touch][gesture] tap -> view=%s\n", availableView ? "remaining" : "used");
  renderAll();
}

static void addTouchCallbacks(lv_obj_t *object, uint8_t detailIndex) {
  lv_obj_add_flag(object, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(object, openDetails, LV_EVENT_LONG_PRESSED, (void *)(uintptr_t)detailIndex);
}

static void makeUsageRow(lv_obj_t *parent, uint8_t index, int x, int y, int width, int height) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_set_size(row, width, height); lv_obj_set_pos(row, x, y);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0); lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 0, 0); lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  rows[index] = row; addTouchCallbacks(row, index);
  lv_obj_t *name = label(row, rowNames[index].c_str(), &lv_font_montserrat_14, C(0xF2F2F2)); lv_obj_set_pos(name, 0, 0);
  statusLabels[index] = label(row, "WAITING", &lv_font_montserrat_12, C(0x888888)); lv_obj_align(statusLabels[index], LV_ALIGN_TOP_RIGHT, -62, 1);
  values[index] = label(row, "--%", &lv_font_montserrat_18, C(0xF2F2F2)); lv_obj_align(values[index], LV_ALIGN_TOP_RIGHT, 0, -2);
  bars[index] = lv_bar_create(row); lv_obj_set_size(bars[index], width, 7); lv_obj_set_pos(bars[index], 0, 19);
  lv_bar_set_range(bars[index], 0, 100); lv_obj_set_style_bg_color(bars[index], C(0x282828), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(bars[index], LV_OPA_COVER, LV_PART_MAIN); lv_obj_set_style_radius(bars[index], 3, LV_PART_MAIN); lv_obj_set_style_radius(bars[index], 3, LV_PART_INDICATOR);
  lv_obj_clear_flag(bars[index], LV_OBJ_FLAG_CLICKABLE);
  barWidths[index] = width; markerY[index] = 17;
  paceMarkers[index] = lv_obj_create(row); lv_obj_set_size(paceMarkers[index], 3, 11); lv_obj_set_pos(paceMarkers[index], 0, markerY[index]);
  lv_obj_set_style_bg_color(paceMarkers[index], C(0xFFFFFF), 0); lv_obj_set_style_bg_opa(paceMarkers[index], LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(paceMarkers[index], 0, 0); lv_obj_set_style_radius(paceMarkers[index], 1, 0); lv_obj_set_style_pad_all(paceMarkers[index], 0, 0);
  lv_obj_clear_flag(paceMarkers[index], LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE); lv_obj_add_flag(paceMarkers[index], LV_OBJ_FLAG_HIDDEN);
  resetLabels[index] = label(row, "Waiting for usage data", &lv_font_montserrat_12, C(0x929292));
  // Keep the reset text visually attached to its own bar. The remaining row
  // height becomes a clear gap before the next metric instead of making the
  // reset look like a subtitle for the row below.
  lv_obj_set_pos(resetLabels[index], 0, max(30, height - 29));
}

static void makePaceRow(lv_obj_t *parent, uint8_t provider, int x, int y, int width, int height) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_set_size(row, width, height); lv_obj_set_pos(row, x, y);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0); lv_obj_set_style_border_width(row, 0, 0); lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE); paceRows[provider] = row; addTouchCallbacks(row, provider == 0 ? 4 : 5);
  lv_obj_t *topLine = lv_obj_create(row); lv_obj_set_size(topLine, width, 1); lv_obj_set_pos(topLine, 0, 0);
  lv_obj_set_style_bg_color(topLine, C(0x242424), 0); lv_obj_set_style_bg_opa(topLine, LV_OPA_COVER, 0); lv_obj_set_style_border_width(topLine, 0, 0); lv_obj_set_style_pad_all(topLine, 0, 0);
  lv_obj_clear_flag(topLine, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t *title = label(row, "LAST 30 MIN", &lv_font_montserrat_14, C(0xF2F2F2)); lv_obj_set_pos(title, 0, 5);
  paceValues[provider] = label(row, "WAITING", &lv_font_montserrat_14, C(0xF2F2F2)); lv_obj_align(paceValues[provider], LV_ALIGN_TOP_RIGHT, 0, 5);
  paceMeta[provider] = label(row, provider == 0 ? "TOKENS / 6 x 5 MIN" : "WEEKLY CHANGE / 6 x 5 MIN", &lv_font_montserrat_12, C(0x929292));
  lv_obj_set_pos(paceMeta[provider], 0, height - 18);
  int chartWidth = 142, gap = 4, columnWidth = (chartWidth - gap * 5) / 6, chartX = width - chartWidth;
  paceChartBaseline[provider] = height - 6;
  paceChartMaxHeight[provider] = constrain(height - 32, 16, 46);
  for (uint8_t i = 0; i < 6; ++i) {
    paceColumns[provider][i] = lv_obj_create(row);
    lv_obj_set_size(paceColumns[provider][i], columnWidth, 2); lv_obj_set_pos(paceColumns[provider][i], chartX + i * (columnWidth + gap), paceChartBaseline[provider] - 2);
    lv_obj_set_style_bg_color(paceColumns[provider][i], C(provider == 0 ? 0x35D078 : 0x8295A8), 0);
    lv_obj_set_style_bg_opa(paceColumns[provider][i], LV_OPA_20, 0); lv_obj_set_style_border_width(paceColumns[provider][i], 0, 0);
    lv_obj_set_style_radius(paceColumns[provider][i], 1, 0); lv_obj_set_style_pad_all(paceColumns[provider][i], 0, 0);
    lv_obj_clear_flag(paceColumns[provider][i], LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  }
}

static lv_obj_t *makeProviderPanel(const char *title, uint8_t provider, int y, int height, bool flat, int &innerX, int &innerWidth) {
  int panelX = flat ? 8 : 12, panelWidth = flat ? 464 : 456;
  innerX = flat ? 8 : 14; innerWidth = panelWidth - innerX * 2;
  lv_obj_t *panel = lv_obj_create(lv_scr_act()); lv_obj_set_size(panel, panelWidth, height); lv_obj_set_pos(panel, panelX, y);
  lv_obj_set_style_pad_all(panel, 0, 0); lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE); lv_obj_clear_flag(panel, LV_OBJ_FLAG_CLICKABLE);
  if (flat) { lv_obj_set_style_bg_opa(panel, LV_OPA_TRANSP, 0); lv_obj_set_style_border_width(panel, 0, 0); }
  else panelStyle(panel);
  lv_obj_t *providerIcon = lv_img_create(panel);
  lv_img_set_src(providerIcon, provider == 0 ? &cursorProviderIcon : &codexProviderIcon);
  lv_obj_set_pos(providerIcon, innerX, 5);
  lv_obj_clear_flag(providerIcon, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  // The supplied Cursor artwork is intentionally very dark. A partial white
  // recolor keeps its shape and shading visible on this dashboard background.
  if (provider == 0) {
    lv_obj_set_style_img_recolor(providerIcon, C(0xFFFFFF), 0);
    lv_obj_set_style_img_recolor_opa(providerIcon, 180, 0);
  }
  lv_obj_t *heading = label(panel, title, &lv_font_montserrat_18, C(0xFFFFFF)); lv_obj_set_pos(heading, innerX + 25, 3);
  providerStatusLabels[provider] = label(panel, "WAITING", &lv_font_montserrat_12, C(0x888888)); lv_obj_align(providerStatusLabels[provider], LV_ALIGN_TOP_RIGHT, -innerX, 6);
  lv_obj_t *divider = lv_obj_create(panel); lv_obj_set_size(divider, innerWidth, 1); lv_obj_set_pos(divider, innerX, 28);
  lv_obj_set_style_bg_color(divider, C(0x3A3A3A), 0); lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0); lv_obj_set_style_border_width(divider, 0, 0); lv_obj_set_style_pad_all(divider, 0, 0);
  lv_obj_clear_flag(divider, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  return panel;
}

void displayBegin(const AppConfig &config) {
  availableView = config.displayAvailable;
  // Bring up the board's independent GT911 bus before the RGB peripheral.
  // This also gives a useful boot-time probe even if LVGL never sees a press.
  touchBegin();
  pinMode(38, OUTPUT); digitalWrite(38, HIGH); gfx->begin(10000000); gfx->fillScreen(BLACK);
  lv_init(); drawMemory = (lv_color_t *)heap_caps_malloc(480 * 32 * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  lv_disp_draw_buf_init(&drawBuf, drawMemory, nullptr, 480 * 32);
  static lv_disp_drv_t displayDriver; lv_disp_drv_init(&displayDriver); displayDriver.hor_res = 480; displayDriver.ver_res = 480;
  displayDriver.flush_cb = flush; displayDriver.draw_buf = &drawBuf; lv_disp_drv_register(&displayDriver);
  static lv_indev_drv_t inputDriver; lv_indev_drv_init(&inputDriver); inputDriver.type = LV_INDEV_TYPE_POINTER;
  inputDriver.read_cb = readTouch; inputDriver.long_press_time = 900;
  touchInputDevice = lv_indev_drv_register(&inputDriver);
  touchLastCallbackMs = millis();
  Serial.printf("[touch][lvgl] Input device registration: %s\n", touchInputDevice ? "ready" : "FAILED");

  lv_obj_set_style_bg_color(lv_scr_act(), C(0x000000), 0); lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, 0);
  lv_obj_add_flag(lv_scr_act(), LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t *title = label(lv_scr_act(), "AI USAGE", &lv_font_montserrat_18, C(0xFFFFFF)); lv_obj_set_pos(title, 16, 6);
  modeLabel = label(lv_scr_act(), "USED", &lv_font_montserrat_12, C(0xFFFFFF)); lv_obj_align(modeLabel, LV_ALIGN_TOP_MID, 0, 11);
  networkLabel = label(lv_scr_act(), "STARTING", &lv_font_montserrat_12, C(0xF2A93B)); lv_obj_align(networkLabel, LV_ALIGN_TOP_RIGHT, -16, 11);

  bool flat = config.displayStyle == 1;
  bool visible[4] = {config.showCursorModels, config.showCursorOther, config.showCursorOnDemand, config.showCodexWeekly};
  uint8_t cursorCount = visible[0] + visible[1] + visible[2];
  uint8_t codexCount = visible[3];
  bool cursorPaceVisible = config.showCursorThirtyMinute;
  bool codexPaceVisible = config.showCodexThirtyMinute;
  uint8_t cursorUnits = cursorCount + (cursorPaceVisible ? 1 : 0);
  uint8_t codexUnits = codexCount + (codexPaceVisible ? 1 : 0);
  uint8_t panelCount = (cursorUnits ? 1 : 0) + (codexUnits ? 1 : 0);
  uint8_t totalUnits = cursorUnits + codexUnits;
  constexpr int panelHeader = 32, panelGap = 4, dashboardTop = 34, dashboardBottom = 478;
  constexpr int dashboardHeight = dashboardBottom - dashboardTop;
  int contentHeight = dashboardHeight - panelHeader * panelCount - panelGap * (panelCount ? panelCount - 1 : 0);
  int unitHeight = totalUnits ? max(46, contentHeight / totalUnits) : 46;
  int remainder = totalUnits ? contentHeight - unitHeight * totalUnits : 0;
  int cursorHeight = cursorUnits ? panelHeader + cursorUnits * unitHeight : 0;
  int codexHeight = codexUnits ? panelHeader + codexUnits * unitHeight : 0;
  if (remainder > 0) {
    if (codexUnits) codexHeight += remainder;
    else cursorHeight += remainder;
  }
  int y = dashboardTop, innerX, innerWidth;

  if (cursorUnits) {
    lv_obj_t *cursorPanel = makeProviderPanel("CURSOR", 0, y, cursorHeight, flat, innerX, innerWidth);
    int rowY = panelHeader;
    uint8_t made = 0;
    int cursorExtra = !codexUnits ? max(0, remainder) : 0;
    for (uint8_t i = 0; i < 3; ++i) if (visible[i]) {
      made++;
      int height = unitHeight + (!cursorPaceVisible && made == cursorCount ? cursorExtra : 0);
      makeUsageRow(cursorPanel, i, innerX, rowY, innerWidth, height); rowY += height;
    }
    if (cursorPaceVisible) makePaceRow(cursorPanel, 0, innerX, rowY, innerWidth, unitHeight + cursorExtra);
    y += cursorHeight + 4;
  }
  if (codexUnits) {
    lv_obj_t *codexPanel = makeProviderPanel("CODEX", 1, y, codexHeight, flat, innerX, innerWidth);
    int rowY = panelHeader;
    if (visible[3]) {
      int height = unitHeight + (!codexPaceVisible ? max(0, remainder) : 0);
      makeUsageRow(codexPanel, 3, innerX, rowY, innerWidth, height); rowY += height;
    }
    if (codexPaceVisible) makePaceRow(codexPanel, 1, innerX, rowY, innerWidth, unitHeight + max(0, remainder));
  }
  Serial.printf("[display] Layout: Cursor units=%u, Codex units=%u, row=%dpx, bottom=%d\n",
                cursorUnits, codexUnits, unitHeight, y + codexHeight);
  renderAll();
}

void displayLoop() {
  lv_timer_handler();
  // Some LVGL builds register the input timer successfully but never schedule
  // its callback. Keep LVGL's complete pointer-event processing intact by
  // invoking that same public timer callback when it has been idle too long.
  uint32_t now = millis();
  if (touchInputDevice && now - touchLastCallbackMs >= 40) {
    touchFallbackReads++;
    lv_indev_read_timer_cb(touchInputDevice->driver->read_timer);
  }
  // Process the raw gesture only after LVGL has finished its input callback.
  // This avoids relying on object hit-testing and avoids changing the object
  // tree while LVGL is still reading the GT911 input device.
  if (touchPendingToggle) {
    touchPendingToggle = false;
    touchToggleEvents++; touchLastEventMs = millis(); touchLastEvent = "view toggled";
    toggleView();
  }
}
void displaySetBrightness(uint8_t value) { digitalWrite(38, value ? HIGH : LOW); }

void displaySetNetwork(const String &text, bool connected) {
  networkAddress = connected ? text : "";
  if (!networkLabel) return;
  lv_label_set_text(networkLabel, text.c_str());
  lv_obj_set_style_text_color(networkLabel, C(connected ? 0x45D597 : 0xF2A93B), 0);
}

TouchDiagnostics displayGetTouchDiagnostics() {
  TouchDiagnostics diagnostics;
  diagnostics.controller = touchAddress ? "GT911" : touchPossibleGsl3680 ? "possible GSL3680" : "none";
  if (touchAddress) {
    if (touchStateReadErrors > 0 && touchStateReads == 0) diagnostics.status = "GT911 detected, but state reads fail";
    else if (touchPointFrames > 0) diagnostics.status = touchFallbackReads > 0 ? "Touch data received via LVGL fallback" : "Touch data received";
    else if (touchReadyFrames > 0) diagnostics.status = "Ready frames received, but no point data";
    else if (touchFallbackReads > 0) diagnostics.status = "GT911 polling active; no touch frame received yet";
    else diagnostics.status = "GT911 detected; no touch frame received yet";
  } else if (touchPossibleGsl3680) diagnostics.status = "No GT911; possible GSL3680 detected at 0x40";
  else if (touchBusDevices == "none") diagnostics.status = "No I2C device detected on touch bus";
  else diagnostics.status = "I2C device found, but no GT911 at 0x5D/0x14";
  diagnostics.busDevices = touchBusDevices;
  diagnostics.lastError = touchLastError;
  diagnostics.lastEvent = touchLastEvent;
  diagnostics.address = touchAddress;
  diagnostics.lastState = touchLastState;
  diagnostics.lastTouchCount = touchLastCount;
  diagnostics.rawWidth = touchRawWidth; diagnostics.rawHeight = touchRawHeight;
  diagnostics.rawX = touchRawX; diagnostics.rawY = touchRawY;
  diagnostics.displayX = touchX; diagnostics.displayY = touchY;
  diagnostics.callbackCalls = touchCallbackCalls;
  diagnostics.fallbackReads = touchFallbackReads;
  diagnostics.probeAttempts = touchProbeAttempts;
  diagnostics.polls = touchPolls;
  diagnostics.stateReads = touchStateReads;
  diagnostics.stateReadErrors = touchStateReadErrors;
  diagnostics.readyFrames = touchReadyFrames;
  diagnostics.pointFrames = touchPointFrames;
  diagnostics.acknowledgeErrors = touchAcknowledgeErrors;
  diagnostics.downEvents = touchDownEvents; diagnostics.upEvents = touchUpEvents;
  diagnostics.tapEvents = touchTapEvents; diagnostics.toggleEvents = touchToggleEvents;
  diagnostics.lastEventMs = touchLastEventMs;
  diagnostics.pressed = touchPressed;
  diagnostics.possibleGsl3680 = touchPossibleGsl3680;
  diagnostics.remainingView = availableView;
  return diagnostics;
}

void displayUpdate(const UsageSnapshot &codex, const UsageSnapshot &cursor, uint8_t warningPercent, uint8_t criticalPercent) {
  latestCodex = codex; latestCursor = cursor; warningLevel = warningPercent; criticalLevel = criticalPercent;
  rowData[0] = cursor.primary; rowData[1] = cursor.secondary; rowData[2] = cursor.tertiary;
  rowData[3] = codex.secondary;
  for (uint8_t i = 0; i < 3; ++i) rowStatus[i] = cursor.status;
  rowStatus[3] = codex.status;
  if (providerStatusLabels[0]) {
    lv_label_set_text(providerStatusLabels[0], cursor.ok ? "ONLINE" : cursor.status == "disabled" ? "DISABLED" : "NO DATA");
    lv_obj_set_style_text_color(providerStatusLabels[0], C(cursor.ok ? 0x45D597 : 0x888888), 0);
  }
  if (providerStatusLabels[1]) {
    lv_label_set_text(providerStatusLabels[1], codex.ok ? "ONLINE" : codex.status == "disabled" ? "DISABLED" : "NO DATA");
    lv_obj_set_style_text_color(providerStatusLabels[1], C(codex.ok ? 0x45D597 : 0x888888), 0);
  }
  renderAll();
  Serial.printf("[display] view=%s, Cursor %.1f / %.1f / %.1f, Codex weekly %.1f\n",
                availableView ? "remaining" : "used", cursor.primary.usedPercent, cursor.secondary.usedPercent,
                cursor.tertiary.usedPercent, codex.secondary.usedPercent);
}
