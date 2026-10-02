#pragma once

#include <stdint.h>
#include <helpers/ui/DisplayDriver.h>

// DisplayDriver backed by the CrowPanel Advance 7's EK79007 panel over 2-lane
// MIPI-DSI (1024x600 RGB565, DPI video mode). Init sequence adapted from DCC's
// production display_driver_p4.cpp for the same hardware (raw IDF esp_lcd DSI
// APIs — no vendor component needed; the EK79007 only needs DCS sleep-out +
// display-on, then a generic DPI panel streams frames).
//
// The touch UI renders with LVGL; UITask's lvglFlush() calls writePixelsRGB565(),
// which blits the dirty area to the DPI panel via esp_lcd_panel_draw_bitmap —
// that's the hot path. The classic DisplayDriver text/shape methods are used
// only for the pre-LVGL boot screen; minimal stubs (same trade-off as
// TanmatsuDisplay — the wadamesh boot mark still renders, via writePixelsRGB565).
//
// Backlight: LEDC PWM on GPIO 31 @ 30 kHz (DCC-verified).
class CrowPanel7Display : public DisplayDriver {
  bool _on = false;
  bool _init_ok = false;
public:
  CrowPanel7Display() : DisplayDriver(1024, 600) {}

  // Full MIPI-DSI bring-up: DPHY LDO -> DSI bus -> DBI io (DCS init) -> DPI
  // panel -> backlight PWM. Returns false (and leaves the UI headless) on any
  // esp_lcd error — errors are printed, never swallowed.
  bool begin();

  // Match the ST7789LCDDisplay surface UITask/main.cpp call on the global `display`:
  void writePixelsRGB565(int x, int y, int w, int h, const uint16_t* pixels);
  void setDisplayRotation(int rot) { (void)rot; /* panel is fixed landscape */ }

  void setBrightness(uint8_t percent);   // 0-100, LEDC duty

  // --- DisplayDriver contract (minimal; LVGL does the real drawing) ---
  // Signatures track core-v1.17.4: the colour parameter is ColorVal (a uint16_t
  // alias), not the old `Color` enum, and startFrame carries the base's default
  // so a no-argument call still resolves through this type. The separate no-arg
  // startFrame()/endFrame() pair this class used to declare is gone — with a
  // defaulted ColorVal overload present it made every display.startFrame()
  // ambiguous.
  bool isOn() override { return _on; }
  void turnOn() override;
  void turnOff() override;
  void clear() override {}
  void startFrame(ColorVal bkg = UIColor::window_bkg) override { (void)bkg; }
  void endFrame() override {}
  void setTextSize(int) override {}
  void setColor(ColorVal) override {}
  void setCursor(int, int) override {}
  void print(const char*) override {}
  void fillRect(int, int, int, int) override {}
  void drawRect(int, int, int, int) override {}
  void drawXbm(int, int, const uint8_t*, int, int) override {}
  uint16_t getTextWidth(const char*) override { return 0; }
};
