// CrowPanel Advance 7" (ESP32-P4) — EK79007 over 2-lane MIPI-DSI.
// Init sequence + timings adapted from DCC's production display_driver_p4.cpp
// for the identical panel (LDO chan 3 @2.5V, 2 lanes @900 Mbps, DPI 52 MHz,
// 1024x600, HS1/HBP160/HFP160, VS1/VBP23/VFP12). Raw IDF esp_lcd APIs only.
#if defined(HAS_CROWPANEL7)

#include <Arduino.h>
#include "CrowPanel7Display.h"

#include <esp_lcd_panel_ops.h>
#include <esp_lcd_mipi_dsi.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_commands.h>
#include <esp_ldo_regulator.h>
#include <driver/ledc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// --- Panel / bus constants (DCC-verified for this hardware) ---
#define CP7_LDO_MIPI_PHY_CHAN   3     // DPHY power, 2.5 V
#define CP7_DSI_LANES           2
#define CP7_DSI_LANE_MBPS       900
#define CP7_DPI_CLK_MHZ         52
#define CP7_LCD_H_RES           1024
#define CP7_LCD_V_RES           600
#define CP7_LCD_HSYNC           1
#define CP7_LCD_HBP             160
#define CP7_LCD_HFP             160
#define CP7_LCD_VSYNC           1
#define CP7_LCD_VBP             23
#define CP7_LCD_VFP             12
#define CP7_BACKLIGHT_PIN       31    // LEDC PWM @ 30 kHz

static esp_lcd_panel_handle_t    s_panel   = nullptr;
static esp_lcd_dsi_bus_handle_t  s_dsi_bus = nullptr;
static esp_lcd_panel_io_handle_t s_io      = nullptr;

static void cp7InitBacklight() {
  ledc_timer_config_t timer_cfg = {};
  timer_cfg.speed_mode      = LEDC_LOW_SPEED_MODE;
  timer_cfg.duty_resolution = LEDC_TIMER_10_BIT;
  timer_cfg.timer_num       = LEDC_TIMER_0;
  timer_cfg.freq_hz         = 30000;
  timer_cfg.clk_cfg         = LEDC_AUTO_CLK;
  ledc_timer_config(&timer_cfg);

  ledc_channel_config_t ch_cfg = {};
  ch_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
  ch_cfg.channel    = LEDC_CHANNEL_0;
  ch_cfg.timer_sel  = LEDC_TIMER_0;
  ch_cfg.gpio_num   = CP7_BACKLIGHT_PIN;
  ch_cfg.duty       = 1023;   // full brightness at boot
  ch_cfg.hpoint     = 0;
  ledc_channel_config(&ch_cfg);
}

bool CrowPanel7Display::begin() {
  if (_init_ok) return true;

  // 1. DPHY power LDO (2.5 V, channel 3)
  esp_ldo_channel_handle_t ldo_mipi = nullptr;
  esp_ldo_channel_config_t ldo_cfg = {};
  ldo_cfg.chan_id    = CP7_LDO_MIPI_PHY_CHAN;
  ldo_cfg.voltage_mv = 2500;
  esp_err_t err = esp_ldo_acquire_channel(&ldo_cfg, &ldo_mipi);
  if (err != ESP_OK) {
    Serial.printf("CP7 DSI: DPHY LDO failed: %d\n", err);
    return false;
  }

  // 2. MIPI-DSI bus, 2 lanes @ 900 Mbps
  esp_lcd_dsi_bus_config_t bus_cfg = {};
  bus_cfg.bus_id             = 0;
  bus_cfg.num_data_lanes     = CP7_DSI_LANES;
  bus_cfg.phy_clk_src        = MIPI_DSI_PHY_CLK_SRC_DEFAULT;
  bus_cfg.lane_bit_rate_mbps = CP7_DSI_LANE_MBPS;
  err = esp_lcd_new_dsi_bus(&bus_cfg, &s_dsi_bus);
  if (err != ESP_OK) {
    Serial.printf("CP7 DSI: bus create failed: %d\n", err);
    return false;
  }

  // 3. DBI IO for the EK79007's DCS init commands
  esp_lcd_dbi_io_config_t dbi_cfg = {};
  dbi_cfg.virtual_channel = 0;
  dbi_cfg.lcd_cmd_bits    = 8;
  dbi_cfg.lcd_param_bits  = 8;
  err = esp_lcd_new_panel_io_dbi(s_dsi_bus, &dbi_cfg, &s_io);
  if (err != ESP_OK) {
    Serial.printf("CP7 DSI: panel IO create failed: %d\n", err);
    return false;
  }

  // 4. EK79007 init: sleep-out + display-on (all this panel needs)
  esp_lcd_panel_io_tx_param(s_io, LCD_CMD_SLPOUT, NULL, 0);
  vTaskDelay(pdMS_TO_TICKS(120));
  esp_lcd_panel_io_tx_param(s_io, LCD_CMD_DISPON, NULL, 0);
  vTaskDelay(pdMS_TO_TICKS(20));

  // 5. DPI video panel (the frame pipe LVGL blits into)
  esp_lcd_dpi_panel_config_t dpi_cfg = {};
  dpi_cfg.dpi_clk_src        = MIPI_DSI_DPI_CLK_SRC_DEFAULT;
  dpi_cfg.dpi_clock_freq_mhz = CP7_DPI_CLK_MHZ;
  dpi_cfg.virtual_channel    = 0;
  dpi_cfg.pixel_format       = LCD_COLOR_PIXEL_FORMAT_RGB565;
  dpi_cfg.video_timing.h_size            = CP7_LCD_H_RES;
  dpi_cfg.video_timing.v_size            = CP7_LCD_V_RES;
  dpi_cfg.video_timing.hsync_pulse_width = CP7_LCD_HSYNC;
  dpi_cfg.video_timing.hsync_back_porch  = CP7_LCD_HBP;
  dpi_cfg.video_timing.hsync_front_porch = CP7_LCD_HFP;
  dpi_cfg.video_timing.vsync_pulse_width = CP7_LCD_VSYNC;
  dpi_cfg.video_timing.vsync_back_porch  = CP7_LCD_VBP;
  dpi_cfg.video_timing.vsync_front_porch = CP7_LCD_VFP;
  err = esp_lcd_new_panel_dpi(s_dsi_bus, &dpi_cfg, &s_panel);
  if (err != ESP_OK) {
    Serial.printf("CP7 DSI: DPI panel create failed: %d\n", err);
    return false;
  }
  err = esp_lcd_panel_init(s_panel);
  if (err != ESP_OK) {
    Serial.printf("CP7 DSI: panel init failed: %d\n", err);
    return false;
  }

  // 6. Backlight on
  cp7InitBacklight();

  Serial.printf("CP7 DSI: EK79007 up (%dx%d, %d lanes @ %d Mbps, DPI %d MHz)\n",
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
