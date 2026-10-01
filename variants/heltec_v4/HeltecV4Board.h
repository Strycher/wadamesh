#pragma once

#include <Arduino.h>
#include <helpers/RefCountedDigitalPin.h>
#include <helpers/ESP32Board.h>
#include <driver/rtc_io.h>
#include "LoRaFEMControl.h"

#ifndef ADC_MULTIPLIER
  // Battery divider ratio for the Heltec V4 family = 4.9.
  //
  // VERIFIED against Heltec's own V4-R8 schematic (HTIT-WBR8H_V4.3.2.pdf,
  // resource.heltec.cn): VBAT -> R27 390K -> ADC_IN -> R28 100K -> GND, with NO
  // series switch. (390+100)/100 = 4.9. Heltec's V4 documentation gives the same
  // 390K/100K pair, so V4 and V4-R8 share the divider.
  //
  // Was 5.42, which reads ~10% high. Combined with the missing ADC attenuation
  // (see begin()), a 3.85 V cell computed to ~4.5 V — above "full" — so the
  // reading sat pinned at 100% and appeared frozen. The V4 TFT env never defines
  // ADC_MULTIPLIER, so this fallback is what that board actually used. (#58)
  //
  // NOT 4.9 * 1.045: Meshtastic's 1.045 compensates for THEIR uncalibrated
  // analogRead() path. getBattMilliVolts() now uses analogReadMilliVolts(),
  // which already applies the chip's eFuse calibration, so the fudge factor
  // would double-correct and over-read by ~4.5%.
  //
  // NOTE: the V4-R8 env defines its own 5.07 and is unaffected by this value —
  // that 5.07 is ~3.5% off the schematic-true 4.9 and should follow (#58).
  #define ADC_MULTIPLIER (4.9f)
#endif

class HeltecV4Board : public ESP32Board {

protected:
  float adc_mult = ADC_MULTIPLIER;

public:
  RefCountedDigitalPin periph_power;
  LoRaFEMControl loRaFEMControl;
  HeltecV4Board() : periph_power(PIN_VEXT_EN,PIN_VEXT_EN_ACTIVE) { }

  void begin();
  void onBeforeTransmit(void) override;
  void onAfterTransmit(void) override;
  void enterDeepSleep(uint32_t secs, int pin_wake_btn = -1);
  void powerOff() override;
  uint16_t getBattMilliVolts() override;
  bool setAdcMultiplier(float multiplier) override {
    if (multiplier == 0.0f) {
      adc_mult = ADC_MULTIPLIER;
    } else {
      adc_mult = multiplier;
    }
    return true;
  }
  float getAdcMultiplier() const override { return adc_mult; }
  const char* getManufacturerName() const override;

  // V4.3 (KCT8103L FEM) high-gain receiver LNA (~17 dB). It is BYPASSED by default; this turns it
  // on/off and re-drives the FEM into RX so the change applies to the live receive immediately.
  // femLnaControllable() is true only on boards where the LNA is switchable (V4.3); a no-op elsewhere.
  void setFemLnaEnable(bool en) { loRaFEMControl.setLNAEnable(en); loRaFEMControl.setRxModeEnable(); }
  bool femLnaControllable() { return loRaFEMControl.isLnaCanControl(); }
};
