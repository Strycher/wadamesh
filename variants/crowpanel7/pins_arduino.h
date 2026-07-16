#ifndef Pins_Arduino_h
#define Pins_Arduino_h

// Elecrow CrowPanel Advance 7 (ESP32-P4 + C6) arduino variant.
//
// EXISTS BECAUSE (#12 bring-up, root cause): the Espressif reference
// `esp32p4` variant hardwires the ESP-Hosted pin table via
// BOARD_HAS_SDIO_ESP_HOSTED, which OUTRANKS every ESP_HOSTED_* Kconfig —
// its RESET 54 is the CrowPanel's LORA RESET line, so the C6 never reset
// and the SX1262 got glitched on every hosted retry. The reference variant
// also defaults MOSI to GPIO32 (the real C6 reset), I2C to 7/8 (our LoRa
// MISO/SCK), and SDMMC power to GPIO45 (our touch SDA) — all landmines.
//
// Pin provenance: CROWPANEL7_PORT.md (DCC-verified C6/display/touch maps +
// Elecrow LoRa-slot docs).

#include <stdint.h>
#include "soc/soc_caps.h"

// Console UART (CH340K on the board's USB-C "UART" port)
static const uint8_t TX = 37;
static const uint8_t RX = 38;

// Default I2C = the touch/RTC bus (GT911 @0x5D; RTC probe 0x51/0x68).
// Explicit Wire.begin(45, 46) is still preferred in our code.
static const uint8_t SDA = 45;
static const uint8_t SCL = 46;

// Default SPI = the Elecrow module slot (SX1262). Matches P_LORA_* build
// flags; keeps any pins-less SPI.begin() away from strap/reset lines.
static const uint8_t SS = 10;
static const uint8_t MOSI = 6;
static const uint8_t MISO = 7;
static const uint8_t SCK = 8;

// On-chip GP LDO: periman enables VO4 when a GPIO in 39-48 is used
// (esp32-hal-ldo.c). That bank carries the touch I2C (45/46) — keep it
// powered exactly like the reference variant does.
#define BOARD_PERIMAN_IO_LDO_AUTO        1
#define BOARD_PERIMAN_IO_LDO0_CHANNEL    4   // LDO_VO4 on ESP32-P4
#define BOARD_PERIMAN_IO_LDO0_GPIO_MIN   39
#define BOARD_PERIMAN_IO_LDO0_GPIO_MAX   48
#define BOARD_PERIMAN_IO_LDO0_VOLTAGE_MV 3300

// NO BOARD_HAS_SDMMC here: the reference board's SDMMC block sets
// POWER_PIN 45 (our touch SDA). CrowPanel SD card = Epic E (#10), with the
// board's real SDMMC slot wiring, not the EV board's.

// WIFI/BLE co-processor - ESP32-C6 over 1-bit SDIO (slot 1)
#define BOARD_HAS_SDIO_ESP_HOSTED
#define BOARD_SDIO_ESP_HOSTED_CLK   18
#define BOARD_SDIO_ESP_HOSTED_CMD   19
#define BOARD_SDIO_ESP_HOSTED_D0    14
#define BOARD_SDIO_ESP_HOSTED_D1    15
#define BOARD_SDIO_ESP_HOSTED_D2    16
#define BOARD_SDIO_ESP_HOSTED_D3    17
#define BOARD_SDIO_ESP_HOSTED_RESET 32   // C6 EN - DCC-verified. NOT 54 (LoRa reset).

#endif /* Pins_Arduino_h */
