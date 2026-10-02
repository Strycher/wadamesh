// CrowPanel Advance 7 touch input — implements the shared touch-input API
// (declared in helpers/input/HeltecV4CapTouch.h) for the panel's GT911
// capacitive controller on I2C (SDA 45 / SCL 46, addr 0x5D — DCC-verified).
//
// Adapted from src/helpers/input/TDeckTouch.cpp (same GT911 silicon, same
// register map). Differences: 1024x600 native-landscape panel whose GT911 is
// factory-configured to the panel resolution, so the raw->screen mapping is
// IDENTITY (DCC's production driver passes raw coords straight to LVGL at
// 1024x600) — no axis swap. Calibration switches kept in case a panel
// revision lands mirrored. No keyboard shares this bus (unlike the T-Deck).
//
// The T-Deck driver is excluded from this env via build_src_filter (its gate
// would otherwise compile it as the default GT911 fallback with wrong pins).
#if defined(HAS_TOUCH_UI) && defined(HAS_CROWPANEL7) && defined(ESP32)

#include "helpers/input/HeltecV4CapTouch.h"
#include <Arduino.h>
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <helpers/ui/MomentaryButton.h>   // BUTTON_EVENT_*

#ifndef PIN_TOUCH_SDA
  #define PIN_TOUCH_SDA 45
#endif
#ifndef PIN_TOUCH_SCL
  #define PIN_TOUCH_SCL 46
#endif
#ifndef PIN_TOUCH_INT
  #define PIN_TOUCH_INT 42
#endif
#ifndef PIN_TOUCH_RST
  #define PIN_TOUCH_RST 40
#endif

static const int SCR_W = 1024;
static const int SCR_H = 600;

// --- Calibration switches (flip if taps land mirrored on a future revision) ---
#ifndef CP7_TS_SWAP_XY
  #define CP7_TS_SWAP_XY 0
#endif
#ifndef CP7_TS_FLIP_X
  #define CP7_TS_FLIP_X 0
#endif
#ifndef CP7_TS_FLIP_Y
  #define CP7_TS_FLIP_Y 0
#endif

static const uint8_t GT911_ADDRS[] = { 0x5D, 0x14 };
static uint8_t  s_addr = 0;
static bool     s_init_ok = false;
static char     s_scan_str[160] = "scan: (not run)";

static volatile uint16_t s_dbg_rawx = 0, s_dbg_rawy = 0;

static bool     s_have_touch = false;
static uint16_t s_cur_x = 0, s_cur_y = 0;
static bool     s_down = false;
static unsigned long s_down_at = 0;
static uint16_t s_start_x = 0, s_start_y = 0;
static uint16_t s_last_x = 0, s_last_y = 0;
static bool     s_live = false;
static uint16_t s_live_x = 0, s_live_y = 0;
static bool     s_tap_pending = false;
static uint16_t s_tap_x = 0, s_tap_y = 0;
static bool     s_swiping_now = false;
static bool     s_swipe_pending = false;
static int8_t   s_swipe_x = 0, s_swipe_y = 0;

static uint8_t  s_ui_rotation = 0;      // informational — panel is fixed landscape
static uint8_t  s_point_rotation = 0;

// Same gesture thresholds as the T-Deck driver, scaled up for the much larger
// panel (40 px of swipe on a 2.4" 320-wide screen ≈ 100 px on this 7" 1024-wide
// one; tap slop scales similarly).
#ifndef CP7_TOUCH_SWIPE_MIN
  #define CP7_TOUCH_SWIPE_MIN 100
#endif
#ifndef CP7_TOUCH_TAP_MOVE_MAX
  #define CP7_TOUCH_TAP_MOVE_MAX 24
#endif
#ifndef CP7_TOUCH_LONG_MS
  #define CP7_TOUCH_LONG_MS 1000
#endif

static bool gt911ReadReg(uint16_t reg, uint8_t* buf, uint8_t len) {
  Wire.beginTransmission(s_addr);
  Wire.write((uint8_t)(reg >> 8));
  Wire.write((uint8_t)(reg & 0xFF));
  if (Wire.endTransmission(false) != 0) return false;
  uint8_t got = Wire.requestFrom((int)s_addr, (int)len);
  if (got != len) return false;
  for (uint8_t i = 0; i < len; ++i) buf[i] = Wire.read();
  return true;
}

static void gt911WriteReg(uint16_t reg, uint8_t val) {
  Wire.beginTransmission(s_addr);
  Wire.write((uint8_t)(reg >> 8));
  Wire.write((uint8_t)(reg & 0xFF));
  Wire.write(val);
  Wire.endTransmission();
}

// Identity mapping (+clamp, + optional calibration flips): the GT911 on this
// panel is factory-configured to the native 1024x600 landscape frame.
static void mapRaw(uint16_t rx, uint16_t ry, uint16_t* ox, uint16_t* oy) {
  int sx = (int)rx;
  int sy = (int)ry;
#if CP7_TS_SWAP_XY
  { int t = sx; sx = sy; sy = t; }
#endif
#if CP7_TS_FLIP_X
  sx = (SCR_W - 1) - sx;
#endif
#if CP7_TS_FLIP_Y
  sy = (SCR_H - 1) - sy;
#endif
  if (sx < 0) sx = 0; if (sx >= SCR_W) sx = SCR_W - 1;
  if (sy < 0) sy = 0; if (sy >= SCR_H) sy = SCR_H - 1;
  *ox = (uint16_t)sx;
  *oy = (uint16_t)sy;
}

static void gt911Poll() {
  uint8_t status;
  if (!gt911ReadReg(0x814E, &status, 1)) return;
  if (!(status & 0x80)) return;                 // no new frame — keep prev state
  uint8_t n = status & 0x0F;
  if (n > 0) {
    uint8_t p[8];
    if (gt911ReadReg(0x8150, p, 8)) {
      uint16_t rx = (uint16_t)(p[0] | (p[1] << 8));
      uint16_t ry = (uint16_t)(p[2] | (p[3] << 8));
      s_dbg_rawx = rx;
      s_dbg_rawy = ry;
      mapRaw(rx, ry, &s_cur_x, &s_cur_y);
      s_have_touch = true;
    }
  } else {
    s_have_touch = false;                        // finger lifted
  }
  gt911WriteReg(0x814E, 0);                       // ack the frame
}

bool heltecV4CapTouchBegin() {
  // One-shot (the UI retries every loop while touch isn't inited).
  static bool s_attempted = false;
  if (s_attempted) return s_init_ok;
  s_attempted = true;

  // GT911 reset: hold RST low 120 ms then release to input (address latches
  // from the INT level at release; the board straps it for 0x5D). The long
  // pulse matters — DCC found a 1 ms pulse leaves the controller unresponsive.
#if PIN_TOUCH_RST >= 0
  pinMode(PIN_TOUCH_RST, OUTPUT);
  digitalWrite(PIN_TOUCH_RST, LOW);
  delay(120);
  pinMode(PIN_TOUCH_RST, INPUT);
  delay(300);   // GT911 post-reset init time (DCC-verified)
#endif

  // Route Wire onto the touch bus. ESP32Board::begin() may have begun Wire on
  // default pins; end() first so the pins actually move (T-Deck lesson).
  Wire.end();
  Wire.begin(PIN_TOUCH_SDA, PIN_TOUCH_SCL);
  Wire.setClock(400000);
  Wire.setTimeOut(20);   // fail fast — a wedged bus must not stall the UI loop
#if PIN_TOUCH_INT >= 0
  pinMode(PIN_TOUCH_INT, INPUT);
#endif
  delay(20);

  // Probe only the GT911 addresses.
  int found = 0;
  int o = snprintf(s_scan_str, sizeof s_scan_str, "i2c%d/%d:", PIN_TOUCH_SDA, PIN_TOUCH_SCL);
  for (uint8_t g = 0; g < sizeof(GT911_ADDRS); ++g) {
    Wire.beginTransmission(GT911_ADDRS[g]);
    if (Wire.endTransmission() == 0) {
      o += snprintf(s_scan_str + o, sizeof s_scan_str - o, " %02X", GT911_ADDRS[g]);
      ++found;
      if (!s_init_ok) { s_addr = GT911_ADDRS[g]; s_init_ok = true; }
    }
  }
  if (found == 0) o += snprintf(s_scan_str + o, sizeof s_scan_str - o, " none");

  // Confirm it's really a GT911 (product-ID reg 0x8140 -> "911") + read the
  // configured output resolution for calibration visibility.
  if (s_init_ok) {
    uint8_t pid[4] = {0};
    if (gt911ReadReg(0x8140, pid, 4) && o < (int)sizeof(s_scan_str) - 14)
      o += snprintf(s_scan_str + o, sizeof s_scan_str - o,
                    " gt@%02X:%c%c%c", s_addr,
                    pid[0] ? pid[0] : '?', pid[1] ? pid[1] : '?',
                    pid[2] ? pid[2] : '?');
    uint8_t res[4] = {0};
    if (gt911ReadReg(0x8048, res, 4) && o < (int)sizeof(s_scan_str) - 14) {
      uint16_t xmax = (uint16_t)(res[0] | (res[1] << 8));
      uint16_t ymax = (uint16_t)(res[2] | (res[3] << 8));
      o += snprintf(s_scan_str + o, sizeof s_scan_str - o, " max%ux%u", xmax, ymax);
    }
  }
  return s_init_ok;
}

void heltecV4CapTouchGetRaw(uint16_t* rx, uint16_t* ry) {
  if (rx) *rx = s_dbg_rawx;
  if (ry) *ry = s_dbg_rawy;
}

int heltecV4CapTouchCheck() {
  if (!s_init_ok) return BUTTON_EVENT_NONE;
  gt911Poll();

  if (s_have_touch) {
    s_live = true;
    s_live_x = s_cur_x;
    s_live_y = s_cur_y;
    if (!s_down) {
      s_down = true;
      s_down_at = millis();
      s_start_x = s_cur_x;
      s_start_y = s_cur_y;
      s_swiping_now = false;
    }
    s_last_x = s_cur_x;
    s_last_y = s_cur_y;
    if (!s_swiping_now) {
      int dx = (int)s_last_x - (int)s_start_x;
      int dy = (int)s_last_y - (int)s_start_y;
      int adx = dx < 0 ? -dx : dx;
      int ady = dy < 0 ? -dy : dy;
      if (adx >= CP7_TOUCH_SWIPE_MIN && adx > ady) s_swiping_now = true;
    }
    return BUTTON_EVENT_NONE;
  }

  if (s_down) {
    s_down = false;
    s_live = false;
    s_swiping_now = false;
    unsigned long dur = millis() - s_down_at;
    int dx = (int)s_last_x - (int)s_start_x;
    int dy = (int)s_last_y - (int)s_start_y;
    int adx = dx < 0 ? -dx : dx;
    int ady = dy < 0 ? -dy : dy;
    if (adx >= CP7_TOUCH_SWIPE_MIN && adx > (ady + 8)) {
      bool left = dx < 0;
      s_swipe_x = left ? -1 : 1;
      s_swipe_y = 0;
      s_swipe_pending = true;
      return left ? BUTTON_EVENT_DOUBLE_CLICK : BUTTON_EVENT_TRIPLE_CLICK;
    }
    if (ady >= CP7_TOUCH_SWIPE_MIN && ady > (adx + 8)) {
      s_swipe_x = 0;
      s_swipe_y = (dy < 0) ? -1 : 1;
      s_swipe_pending = true;
      return BUTTON_EVENT_NONE;
    }
    if (dur >= 12 && dur < (unsigned long)CP7_TOUCH_LONG_MS &&
        adx <= CP7_TOUCH_TAP_MOVE_MAX && ady <= CP7_TOUCH_TAP_MOVE_MAX) {
      s_tap_x = s_last_x;
      s_tap_y = s_last_y;
      s_tap_pending = true;
      return BUTTON_EVENT_CLICK;
    }
  } else {
    s_live = false;
  }
  return BUTTON_EVENT_NONE;
}

bool heltecV4CapTouchGetLive(uint16_t* x, uint16_t* y) {
  if (!s_live) return false;
  if (x) *x = s_live_x;
  if (y) *y = s_live_y;
  return true;
}

bool heltecV4CapTouchPopTap(uint16_t* x, uint16_t* y) {
  if (!s_tap_pending) return false;
  s_tap_pending = false;
  if (x) *x = s_tap_x;
  if (y) *y = s_tap_y;
  return true;
}

bool heltecV4CapTouchPopSwipe(int8_t* xd, int8_t* yd) {
  if (!s_swipe_pending) return false;
  s_swipe_pending = false;
  if (xd) *xd = s_swipe_x;
  if (yd) *yd = s_swipe_y;
  return true;
}

// Background poll on core 0 so GT911 I2C round-trips never stall the LVGL
// render / mesh loop (same rationale as the V4 + T-Deck drivers).
static TaskHandle_t s_poll_task = nullptr;
static volatile bool s_async = false;
static uint32_t s_period_ms = 8;

static void touchPollTask(void* arg) {
  (void)arg;
  for (;;) {
    heltecV4CapTouchCheck();
    vTaskDelay(pdMS_TO_TICKS(s_period_ms));
  }
}

bool heltecV4CapTouchStartBackgroundPoll(uint32_t period_ms) {
  if (s_async || !s_init_ok) return false;
  s_period_ms = period_ms < 4 ? 4 : (period_ms > 100 ? 100 : period_ms);
  BaseType_t ok = xTaskCreatePinnedToCore(touchPollTask, "cp7_touch", 3072,
                                          nullptr, 2, &s_poll_task, 0);
  if (ok == pdPASS) { s_async = true; return true; }
  return false;
}
bool heltecV4CapTouchIsAsyncPolling() { return s_async; }
bool heltecV4CapTouchIsSwiping() { return s_swiping_now; }
void heltecV4CapTouchSetRotation(uint8_t r) { s_ui_rotation = r & 3; }
void heltecV4CapTouchSetPointRotation(uint8_t r) { s_point_rotation = r & 3; }

const char* heltecV4CapTouchDebug() { return s_scan_str; }

#endif // HAS_TOUCH_UI && HAS_CROWPANEL7 && ESP32
