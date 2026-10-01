#include "HeltecV4Board.h"

void HeltecV4Board::begin() {
    ESP32Board::begin();


#if defined(PIN_ADC_CTRL) && PIN_ADC_CTRL >= 0
    pinMode(PIN_ADC_CTRL, OUTPUT);
    digitalWrite(PIN_ADC_CTRL, LOW); // Initially inactive
#endif  // V4-R8: no ADC-control MOSFET — the battery divider is read directly

    // Battery ADC attenuation. Meshtastic's heltec_v4 variant sets
    // ADC_ATTEN_DB_2_5 explicitly, commented "lower dB for high resistance
    // voltage divider" — this divider is high-impedance and the S3's ADC is at
    // its worst (most non-linear) near the top of the 11 dB range, which is
    // exactly where a charged cell sits. We previously set NO attenuation at
    // all, inheriting Arduino's 11 dB default while getBattMilliVolts()
    // converted with a linear 3.3 V full-scale assumption — so both the scale
    // and the linearity were wrong. (#58)
    // Headroom check: divider output at a full 4.2 V cell is ~820 mV on the V4
    // (÷5.12) and ~830 mV on the R8 (÷5.07), both comfortably inside 2.5 dB
    // full-scale (~1250 mV on ESP32-S3), so nothing clips.
    analogSetPinAttenuation(PIN_VBAT_READ, ADC_2_5db);

    loRaFEMControl.init();

#if defined(HELTEC_V4_EXPANSION_IO_PIN) && !defined(HELTEC_LORA_V4_R8)
    pinMode(HELTEC_V4_EXPANSION_IO_PIN, INPUT_PULLUP);
#endif

#if defined(HELTEC_LORA_V4_R8) && defined(PIN_SD_CS)
    // Park the shared-FSPI micro-SD's chip-select HIGH before LGFX starts
    // driving the bus (display.begin + logo blit run before the first
    // SD.begin): GPIO3 is a strapping pin with no firmware pull, and a card
    // that samples CS low during that window can latch a confused state the
    // mount ladder then has to rescue. Same discipline as M9Board/the pager.
    pinMode(PIN_SD_CS, OUTPUT);
    digitalWrite(PIN_SD_CS, HIGH);
    // The R8 boot FEM-type line makes the auto-detect visible (the About page
    // reports the board name only) — needed to verify GPIO46 really is the
    // FEM TX-enable on this board rather than a plain LED (see r8 audit).
    Serial.printf("[R8] FEM type=%d (0=GC1109 1=KCT8103L 2=other)\n", (int)loRaFEMControl.getFEMType());
#endif

    periph_power.begin();
    esp_reset_reason_t reason = esp_reset_reason();
    if (reason == ESP_RST_DEEPSLEEP) {
      long wakeup_source = esp_sleep_get_ext1_wakeup_status();
      if (wakeup_source & (1 << P_LORA_DIO_1)) {  // received a LoRa packet (while in deep sleep)
        startup_reason = BD_STARTUP_RX_PACKET;
    }

      rtc_gpio_hold_dis((gpio_num_t)P_LORA_NSS);
      rtc_gpio_deinit((gpio_num_t)P_LORA_DIO_1);
#if defined(HELTEC_LORA_V4_R8)
      // Release the digital-pad holds powerOffCb armed before deep sleep
      // (VEXT / GPS_EN / backlight) — held pads would otherwise block this
      // boot's own rail and backlight drives.
      gpio_deep_sleep_hold_dis();
      gpio_hold_dis((gpio_num_t)PIN_VEXT_EN);
      gpio_hold_dis((gpio_num_t)PIN_GPS_EN);
      gpio_hold_dis((gpio_num_t)PIN_TFT_LEDA_CTL);
#endif
    }
  }

  void HeltecV4Board::onBeforeTransmit(void) {
#ifdef P_LORA_TX_LED
    digitalWrite(P_LORA_TX_LED, HIGH);   // turn TX LED on
#endif
    loRaFEMControl.setTxModeEnable();
  }

  void HeltecV4Board::onAfterTransmit(void) {
#ifdef P_LORA_TX_LED
    digitalWrite(P_LORA_TX_LED, LOW);   // turn TX LED off
#endif
    loRaFEMControl.setRxModeEnable();
  }

  void HeltecV4Board::enterDeepSleep(uint32_t secs, int pin_wake_btn) {
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

    // Make sure the DIO1 and NSS GPIOs are hold on required levels during deep sleep
    rtc_gpio_set_direction((gpio_num_t)P_LORA_DIO_1, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pulldown_en((gpio_num_t)P_LORA_DIO_1);

    rtc_gpio_hold_en((gpio_num_t)P_LORA_NSS);

    loRaFEMControl.setRxModeEnableWhenMCUSleep();//It also needs to be enabled in receive mode

    if (pin_wake_btn < 0) {
      esp_sleep_enable_ext1_wakeup( (1L << P_LORA_DIO_1), ESP_EXT1_WAKEUP_ANY_HIGH);  // wake up on: recv LoRa packet
    } else {
      esp_sleep_enable_ext1_wakeup( (1L << P_LORA_DIO_1) | (1L << pin_wake_btn), ESP_EXT1_WAKEUP_ANY_HIGH);  // wake up on: recv LoRa packet OR wake btn
    }

    if (secs > 0) {
      esp_sleep_enable_timer_wakeup(secs * 1000000);
    }

    // Finally set ESP32 into sleep
    esp_deep_sleep_start();   // CPU halts here and never returns!
  }

  void HeltecV4Board::powerOff()  {
    enterDeepSleep(0);
  }

  uint16_t HeltecV4Board::getBattMilliVolts()  {
#if defined(HELTEC_LORA_V4_R8)
    // R8: match Meshtastic's heltec_v4_r8 variant — the always-connected
    // high-resistance divider wants LOW attenuation (2.5 dB, full-scale
    // ~1.05 V; the node sits at VBAT/5.07 ≈ 0.65-0.85 V) and a CALIBRATED
    // millivolt read. The generic uncalibrated raw*(3.3/1024) at the default
    // 11 dB under-read this divider by ~5-10%: a full 4.2 V pack displayed
    // ~3.9 V, so the charging-bolt threshold (batteryFullMv()+50) was
    // unreachable by construction and one ADC count quantized to ~16 mV of
    // display (the "frozen at 3839 mV" report).
    static bool s_atten_set = false;
    if (!s_atten_set) { analogSetPinAttenuation(PIN_VBAT_READ, ADC_2_5db); s_atten_set = true; }
    uint32_t mv = 0;
    for (int i = 0; i < 8; i++) mv += analogReadMilliVolts(PIN_VBAT_READ);
    return (uint16_t)((mv / 8) * adc_mult);
#else
    // Non-R8 V4 (incl. the HV4.3 TFT): beta_85 fixed only the R8 branch above
    // and left this one on the uncalibrated raw*(3.3/1024) read. The same
    // argument applies here, so #58's calibrated path is kept for the rest of
    // the V4 family (see 6ac6dfd, "both V4 and V4-R8").
#if defined(PIN_ADC_CTRL) && PIN_ADC_CTRL >= 0
    digitalWrite(PIN_ADC_CTRL, HIGH);   // enable the battery divider
    delay(10);
#endif
    // Read CALIBRATED millivolts rather than raw counts. analogReadMilliVolts()
    // applies the chip's eFuse ADC calibration for the configured attenuation
    // (set to 2.5 dB in begin()), which removes both the full-scale guess and
    // the S3 ADC's non-linearity. The old path was:
    //     analogReadResolution(10); raw = analogRead(); (adc_mult * 3.3/1024 * raw)
    // which hardcoded a 3.3 V full scale that did not match the attenuation in
    // force, and treated a non-linear ADC as linear. (#58)
    uint32_t mv_sum = 0;
    for (int i = 0; i < 8; i++) {
      mv_sum += analogReadMilliVolts(PIN_VBAT_READ);
    }
    const uint32_t pin_mv = mv_sum / 8;   // millivolts at the divider tap
#if defined(PIN_ADC_CTRL) && PIN_ADC_CTRL >= 0
    digitalWrite(PIN_ADC_CTRL, LOW);
#endif
    return (uint16_t)(adc_mult * (float)pin_mv);   // scale back up to cell volts
#endif
  }

  const char* HeltecV4Board::getManufacturerName() const {
#if defined(HELTEC_LORA_V4_R8)
    return "Heltec V4-R8";
#elif defined(HELTEC_LORA_V4_TFT)
    return loRaFEMControl.getFEMType() == KCT8103L_PA ? "Heltec V4.3 TFT" : "Heltec V4 TFT";
#else
    return loRaFEMControl.getFEMType() == KCT8103L_PA ? "Heltec V4.3 OLED" : "Heltec V4 OLED";
#endif
  }
