// CrowPanel Advance 7" (ESP32-P4) — EK79007 over 2-lane MIPI-DSI.
//
// REWRITTEN after first bring-up (#12): the original hand-rolled init sent
// only SLPOUT+DISPON — the panel was never configured, backlight lit over a
// dead stream. This version mirrors DCC's PRODUCTION display_p4.cpp
// step-for-step (verified working on this exact board 2026-07-15 via the
// DCC-image board-validation flash):
//   1. LDO3 2.5 V  — MIPI DPHY power
//   2. LDO4 3.3 V  — VO4 rail for the GPIO39-48 IO bank (touch I2C 45/46,
//      TP INT 42 / RST 40). DCC acquires this explicitly; without it the
//      whole bank is unpowered (also why our GT911 never answered).
//   3. Backlight LEDC @30 kHz on GPIO31 (off until the panel streams)
//   4. DSI bus 2 lanes @900 Mbps → DBI IO → esp_lcd_ek79007 VENDOR driver
//      (full DCS init table; vendored at components/esp_lcd_ek79007, pinned
//      1.0.4 = DCC's) → panel_reset → panel_init
//   5. DPI 51 MHz, HS 70/160/160, VS 10/23/12, RGB565, DMA2D on
// Every step logs "[DSP] ..." so serial pinpoints any failure (DCC logging
// discipline).
#if defined(HAS_CROWPANEL7)

#include <Arduino.h>
#include "CrowPanel7Display.h"

#include <esp_lcd_panel_ops.h>
#include <esp_lcd_mipi_dsi.h>
#include <esp_lcd_panel_io.h>
#include <esp_ldo_regulator.h>
#include <driver/gpio.h>
#include <driver/ledc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "esp_lcd_ek79007.h"

#define CP7_LDO_MIPI_PHY_CHAN   3     // DPHY power, 2.5 V
#define CP7_LDO_IOBANK_CHAN     4     // VO4: GPIO39-48 bank, 3.3 V (touch bus)
#define CP7_DSI_LANES           2
#define CP7_DSI_LANE_MBPS       900
#define CP7_DPI_CLK_MHZ         51
#define CP7_LCD_H_RES           1024
#define CP7_LCD_V_RES           600
#define CP7_LCD_HSYNC           70
#define CP7_LCD_HBP             160
#define CP7_LCD_HFP             160
#define CP7_LCD_VSYNC           10
#define CP7_LCD_VBP             23
#define CP7_LCD_VFP             12
#define CP7_BACKLIGHT_PIN       31    // LEDC PWM @ 30 kHz

static esp_lcd_panel_handle_t    s_panel   = nullptr;
static esp_lcd_dsi_bus_handle_t  s_dsi_bus = nullptr;
static esp_lcd_panel_io_handle_t s_io      = nullptr;
static esp_ldo_channel_handle_t  s_ldo3    = nullptr;
static esp_ldo_channel_handle_t  s_ldo4    = nullptr;

#define DSP_STEP(tag, call)                                    \
  do {                                                         \
    esp_err_t _e = (call);                                     \
    if (_e != ESP_OK) {                                        \
      Serial.printf("[DSP] " tag " FAILED: 0x%x\n", _e);       \
      return false;                                            \
    }                                                          \
    Serial.println("[DSP] " tag " ok");                        \
  } while (0)

static esp_err_t cp7BacklightInit() {
  gpio_config_t io_cfg = {};
  io_cfg.pin_bit_mask = (1ULL << CP7_BACKLIGHT_PIN);
  io_cfg.mode         = GPIO_MODE_OUTPUT;
  esp_err_t err = gpio_config(&io_cfg);
  if (err != ESP_OK) return err;

  ledc_timer_config_t timer_cfg = {};
  timer_cfg.speed_mode      = LEDC_LOW_SPEED_MODE;
  timer_cfg.duty_resolution = LEDC_TIMER_10_BIT;
  timer_cfg.timer_num       = LEDC_TIMER_0;
  timer_cfg.freq_hz         = 30000;
  timer_cfg.clk_cfg         = LEDC_AUTO_CLK;
  err = ledc_timer_config(&timer_cfg);
  if (err != ESP_OK) return err;

  ledc_channel_config_t ch_cfg = {};
  ch_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
  ch_cfg.channel    = LEDC_CHANNEL_0;
  ch_cfg.timer_sel  = LEDC_TIMER_0;
  ch_cfg.gpio_num   = CP7_BACKLIGHT_PIN;
  ch_cfg.duty       = 0;              // off until the panel streams (DCC order)
  ch_cfg.hpoint     = 0;
  return ledc_channel_config(&ch_cfg);
}

bool CrowPanel7Display::begin() {
  if (_init_ok) return true;

  // 1-2. Power rails (DCC order: DPHY first, then the IO bank)
  esp_ldo_channel_config_t ldo3_cfg = {};
  ldo3_cfg.chan_id    = CP7_LDO_MIPI_PHY_CHAN;
  ldo3_cfg.voltage_mv = 2500;
  DSP_STEP("ldo3 2.5V (DPHY)", esp_ldo_acquire_channel(&ldo3_cfg, &s_ldo3));

  esp_ldo_channel_config_t ldo4_cfg = {};
  ldo4_cfg.chan_id    = CP7_LDO_IOBANK_CHAN;
  ldo4_cfg.voltage_mv = 3300;
  DSP_STEP("ldo4 3.3V (IO bank 39-48)", esp_ldo_acquire_channel(&ldo4_cfg, &s_ldo4));

  // 3. Backlight (configured, held dark until init completes)
  DSP_STEP("backlight ledc", cp7BacklightInit());

  // 4a. DSI bus
  esp_lcd_dsi_bus_config_t bus_cfg = {};
  bus_cfg.bus_id             = 0;
  bus_cfg.num_data_lanes     = CP7_DSI_LANES;
  bus_cfg.phy_clk_src        = MIPI_DSI_PHY_CLK_SRC_DEFAULT;
  bus_cfg.lane_bit_rate_mbps = CP7_DSI_LANE_MBPS;
  DSP_STEP("dsi bus", esp_lcd_new_dsi_bus(&bus_cfg, &s_dsi_bus));

  // 4b. DBI IO for the vendor driver's DCS init table
  esp_lcd_dbi_io_config_t dbi_cfg = {};
  dbi_cfg.virtual_channel = 0;
  dbi_cfg.lcd_cmd_bits    = 8;
  dbi_cfg.lcd_param_bits  = 8;
  DSP_STEP("dbi io", esp_lcd_new_panel_io_dbi(s_dsi_bus, &dbi_cfg, &s_io));

  // 5. DPI video config (DCC's exact timings)
  esp_lcd_dpi_panel_config_t dpi_cfg = {};
  dpi_cfg.virtual_channel    = 0;
  dpi_cfg.dpi_clk_src        = MIPI_DSI_DPI_CLK_SRC_DEFAULT;
  dpi_cfg.dpi_clock_freq_mhz = CP7_DPI_CLK_MHZ;
  dpi_cfg.pixel_format       = LCD_COLOR_PIXEL_FORMAT_RGB565;
  dpi_cfg.num_fbs            = 1;
  dpi_cfg.video_timing.h_size            = CP7_LCD_H_RES;
  dpi_cfg.video_timing.v_size            = CP7_LCD_V_RES;
  dpi_cfg.video_timing.hsync_pulse_width = CP7_LCD_HSYNC;
  dpi_cfg.video_timing.hsync_back_porch  = CP7_LCD_HBP;
  dpi_cfg.video_timing.hsync_front_porch = CP7_LCD_HFP;
  dpi_cfg.video_timing.vsync_pulse_width = CP7_LCD_VSYNC;
  dpi_cfg.video_timing.vsync_back_porch  = CP7_LCD_VBP;
  dpi_cfg.video_timing.vsync_front_porch = CP7_LCD_VFP;
  dpi_cfg.flags.use_dma2d    = true;

  // 4c-5. EK79007 vendor driver: sends the panel's REAL init table.
  ek79007_vendor_config_t vendor_cfg = {};
  vendor_cfg.mipi_config.dsi_bus    = s_dsi_bus;
  vendor_cfg.mipi_config.dpi_config = &dpi_cfg;

  esp_lcd_panel_dev_config_t panel_cfg = {};
  panel_cfg.reset_gpio_num = -1;                       // DCS soft reset
  panel_cfg.rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB;
  panel_cfg.bits_per_pixel = 16;
  panel_cfg.vendor_config  = &vendor_cfg;
  DSP_STEP("ek79007 create", esp_lcd_new_panel_ek79007(s_io, &panel_cfg, &s_panel));
  DSP_STEP("panel reset", esp_lcd_panel_reset(s_panel));
  DSP_STEP("panel init", esp_lcd_panel_init(s_panel));

  // 6. Panel streaming — light it up.
  setBrightness(100);
  Serial.printf("[DSP] EK79007 up (%dx%d, %d lanes @ %d Mbps, DPI %d MHz, vendor-init)\n",
                CP7_LCD_H_RES, CP7_LCD_V_RES, CP7_DSI_LANES,
                CP7_DSI_LANE_MBPS, CP7_DPI_CLK_MHZ);
  _init_ok = true;
  _on = true;
  return true;
}

void CrowPanel7Display::writePixelsRGB565(int x, int y, int w, int h, const uint16_t* pixels) {
  if (!s_panel) return;
  // draw_bitmap takes END-EXCLUSIVE coords (same convention DCC's flush used).
  esp_lcd_panel_draw_bitmap(s_panel, x, y, x + w, y + h, pixels);
}

void CrowPanel7Display::setBrightness(uint8_t percent) {
  if (percent > 100) percent = 100;
  uint32_t duty = (uint32_t)percent * 1023 / 100;
  ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

void CrowPanel7Display::turnOn() {
  _on = true;
  setBrightness(100);
}

void CrowPanel7Display::turnOff() {
  // DPI video mode keeps streaming; "off" = backlight to zero (panel content
  // invisible, power saving is marginal on a mains HMI).
  _on = false;
  setBrightness(0);
}

#endif // HAS_CROWPANEL7
