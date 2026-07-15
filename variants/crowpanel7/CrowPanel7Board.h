#pragma once

#include <Arduino.h>
#include <helpers/ESP32Board.h>

// ---------------------------------------------------------------------------
// Elecrow CrowPanel Advance 7" — ESP32-P4 HMI (1024x600 MIPI-DSI, GT911 touch).
//
// Mains/USB-C powered, no battery — getBattMilliVolts() returns 0 so the UI's
// battery widget reads "no battery" rather than a bogus ADC value (the base
// class's 2:1-divider read on a floating pin).
//
// Wireless co-processor: onboard ESP32-C6 via ESP-Hosted over 1-bit SDIO
// (CMD 19 / CLK 18 / D0 14 / D1 15, slave reset GPIO 32 active-high) — pin map
// carried verbatim from DCC's production sdkconfig.defaults.crowpanel7 for the
// same hardware; configured via the env's custom_sdkconfig, not here.
//
// LoRa: Elecrow SX1262 module in the wireless-module slot, DIRECT SPI to the
// P4 (CLK 8 / MISO 7 / MOSI 6, NSS 10 / BUSY 9 / DIO1 53 / NRST 54 — verified
// from Elecrow-RD's bsp_wireless.h example). See variants/crowpanel7/target.cpp.
// ---------------------------------------------------------------------------

class CrowPanel7Board : public ESP32Board {
public:
  void begin() {
    ESP32Board::begin();
  }

  uint16_t getBattMilliVolts() override {
    return 0;   // no battery — mains-powered HMI
  }

  const char* getManufacturerName() const override {
    return "Elecrow CrowPanel Advance 7 P4";
  }
};
