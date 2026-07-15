#include <Arduino.h>
#include "target.h"

// Set phase 1 when target.cpp globals start (no Serial - can crash before setup).
namespace {
struct BootTrace {
  BootTrace() { set_boot_phase(1); }
} _boot_trace;
}

CrowPanel7Board board;

// Elecrow module-slot SPI is dedicated to the SX1262 (CLK 8 / MISO 7 / MOSI 6)
// — the display is MIPI-DSI and the SD/co-proc are on SDMMC, so the radio gets
// its own SPIClass instance, same shape as the Heltec V4 variant. std_init()
// begins the bus on the P_LORA_* pins.
static SPIClass spi;
RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, spi);

WRAPPER_CLASS radio_driver(radio, board);

ESP32RTCClock fallback_clock;
ClockFloorRTC rtc_clock(fallback_clock);   // monotonic send-timestamp floor (#89)

// No GPS / env sensors on this HMI (ENV_INCLUDE_* all 0 in platformio.ini).
EnvironmentSensorManager sensors;

#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true);
#endif

bool radio_init() {
  fallback_clock.begin();
  // Wire is routed to the touch bus (SDA 45 / SCL 46) — the only external I2C
  // on this board. AutoDiscover probes RTC addresses (0x51/0x68); the GT911
  // sits at 0x5D and is never addressed by the probe.
  rtc_clock.begin(Wire);

  // TCXO 1.6 V + no DIO2 RF switch: from Elecrow's own RadioLib example for
  // this module (begin(..., 1.6), no setDio2AsRfSwitch call). The DIO2
  // assumption is [hypothesis] until RF bring-up — if TX is ~16 dB down
  // (T-Deck #6 symptom), revisit SX126X_DIO2_AS_RF_SWITCH first.
  return radio.std_init(&spi);
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);  // create new random identity
}
