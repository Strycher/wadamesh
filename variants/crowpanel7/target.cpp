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

// ESP-Hosted slave-reset pin (#12 bring-up root cause): arduino's reference
// `esp32p4` variant pins_arduino.h hardwires BOARD_SDIO_ESP_HOSTED_RESET 54 —
// the CrowPanel's LORA RESET — and that table OUTRANKS every ESP_HOSTED_*
// Kconfig. In hybrid builds `board_build.variants_dir` is dead (arduino's
// CMake only reads variants/ inside its own package), so the sanctioned fix
// is hostedSetPins() BEFORE any WiFi/hosted call: it rewrites the same static
// table arduino's hostedInit() consumes — no config race (never pre-latch
// esp_hosted_sdio_set_config(): hostedInit hard-fails on ALREADY_SET and the
// half-up netif then faults in lwip; learned the hard way, see #12).
// Correct reset = GPIO32 (C6 EN, DCC-verified). variants/crowpanel7/
// pins_arduino.h documents the full intended variant for a future
// non-hybrid build.
//
// SDIO DATA LINES ARE BOARD-REV-SPECIFIC (#12 real root cause). Elecrow
// REVERSED the P4<->C6 data routing between V1.0 and V1.1/V1.2 (verified in
// Eagle_SCH&PCB/{1.0,1.1,1.2}.sch):
//   V1.0        : D0=14 D1=15 D2=16 D3=17   (DCC's unit — why 14/15 works there)
//   V1.1 / V1.2 : D0=17 D1=16 D2=15 D3=14   (reversed — THIS unit)
// CLK=18/CMD=19 unchanged across revs, so a wrong data map still passes SDIO
// card-init (CMD+CLK only) but the DAT1 slave-ready interrupt lands on a dead
// pin -> "Card init success" then "Waiting for slave to be ready" forever.
// That symptom cost most of this session; it was a copied-from-DCC config, not
// a bad C6. If a future unit is V1.0, swap back to 14/15/16/17.
#include "esp32-hal-hosted.h"
static void setHostedPinsForCrowPanel() {
  if (!hostedSetPins(/*clk*/18, /*cmd*/19, /*d0*/17, /*d1*/16,
                     /*d2*/15, /*d3*/14, /*rst*/32)) {
    Serial.println("[BOOT] hostedSetPins REFUSED (hosted already up?) - pins may be wrong");
  } else {
    Serial.println("[BOOT] hosted sdio pins set (V1.1/V1.2: d0=17 d1=16, rst=32)");
  }
}

bool radio_init() {
  setHostedPinsForCrowPanel();
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
