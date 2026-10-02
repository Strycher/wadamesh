# CrowPanel 7 (ESP32-P4) port — working tracker

Goal: run the **full** wadamesh UI + companion on the **Elecrow CrowPanel Advance 7"
ESP32-P4 HMI**, kept as **one codebase** with the S3 boards + Tanmatsu. Tracks epic
Strycher/wadamesh#2. This file is the running plan/status — update as we go.

Status: **Epic A (#6) BUILD GREEN 2026-07-15: the FULL app — MeshCore core +
29k-line LVGL UITask + RadioLib SX1262 + hosted-WiFi stack — compiles and links for
the P4.** `pio run -e crowpanel7_companion_radio_touch` → firmware.bin +
firmware.factory.bin (bootloader @0x2000), RAM 9.7% / Flash 65% of the 3.875 MB app0
slot. Dev unit enrolled as `crowpanel7-dev` (see #5 for the CH340K hub-hash saga).
**No flashing authorized yet** — on-device bring-up (display/touch/radio
verification, Epics B–D) is human-gated.

## Build lessons (12 iterations to green — for the next P4 board)

1. `custom_sdkconfig` is a pure-arduino pioarduino feature — the hybrid
   (arduino+espidf) configure ignores it. Use a real defaults file via
   `board_build.cmake_extra_args = -DSDKCONFIG_DEFAULTS=...` (DCC pattern).
2. The generated `sdkconfig.<env>` at repo root is NOT regenerated when the
   defaults file changes — delete it to re-seed. (It's a derived artifact.)
3. `CONFIG_AUTOSTART_ARDUINO=y` is mandatory: gates setup()/loop() AND the
   `feedLoopWDT()` declaration UITask uses.
4. S3-isms guarded: `soc/rtc_cntl_reg.h` + `RTC_CNTL_OPTION1` download-boot
   (now `!CONFIG_IDF_TARGET_ESP32P4`, generalizing the HAS_TANMATSU guards),
   `esp_task_wdt_init(int,bool)` → IDF5 `esp_task_wdt_reconfigure(cfg)`,
   ext0 deep-sleep wake gated `PIN_USER_BTN >= 0` (P4 has no ext0).
5. BT/NimBLE fully out until Epic D: `CONFIG_BT_ENABLED=n` + NimBLE-Arduino
   removed from lib_deps (its headers force-include esp_bt.h, absent on P4).
6. LDF chain mode doesn't evaluate #if guards → it auto-installed the ancient
   AVR "SD" registry lib; `lib_ignore = SD`.
7. `tool-esptoolpy` package can end up manifest-only (uv "not a Python
   project" ×3 per build + bootloader.bin ModuleNotFoundError). Payload lands
   in `.platformio/tools/tool-esptoolpy`; mirror it into
   `.platformio/packages/tool-esptoolpy` if the platform's copy step half-completes.
8. **`chip_variant: esp32p4_es` in the board json is load-bearing**: without
   it the generated linker scripts assume rev≥3.0 contiguous SRAM and the link
   dies on `_bss_start_low/_heap_start_high` etc. (+ `sram_seg` warning). The
   ES variant matches DCC's rev v1.3 silicon and the REV_LESS_V3 sdkconfig.
9. arduino 3.x ssl_client ABI mismatch on P4 → `ssl_client_stub.cpp` (verbatim
   tanmatsu pattern, HAS_CROWPANEL7 gate). Revisit for HTTPS OTA in Epic E.
10. `build_src_filter` minus-entries did NOT exclude TDeckTouch.cpp on this
    env (cause unconfirmed — comment-pollution was fixed and it persisted);
    the driver is now hard-gated `!defined(HAS_CROWPANEL7)` like its HELTEC
    gate. Filter mystery parked, not blocking.
11. Comment lines INSIDE multiline platformio.ini values become value lines
    (configparser) — keep option lists pure.
12. adcAttachPin gone in arduino 3.x → force-included crowpanel7_compat.h shim
    (tanmatsu pattern).

---

## Hardware facts (all `[verified]` unless noted)

| | | source |
|---|---|---|
| Main SoC | ESP32-P4 (DCC unit: rev v1.3/eco2, 360 MHz for rev<3.0) | DCC `sdkconfig.defaults.crowpanel7` |
| PSRAM / flash | hex-mode PSRAM @200 MHz / 16 MB QIO | DCC sdkconfig |
| Display | 7" IPS 1024×600, **EK79007** panel, **2-lane MIPI-DSI** (900 Mbps, DPI 52 MHz, HS1/HBP160/HFP160, VS1/VBP23/VFP12), DPHY LDO chan 3 @2.5 V | DCC `display_driver_p4.{h,cpp}` |
| Backlight | PWM GPIO **31** @30 kHz (LEDC) | DCC |
| Touch | **GT911** I2C addr 0x5D @400 kHz, SDA **45** / SCL **46** / INT 42 / RST 40 | DCC `pins_config.h` |
| Wireless co-proc | onboard **ESP32-C6** (WiFi-6/BLE) via **ESP-Hosted over 1-bit SDIO**: CMD 19 / CLK 18 / D0 14 / D1 15, slave reset GPIO **32** active-high, 1500 ms delay, 40 MHz | DCC sdkconfig (known-good in production at desk) |
| LoRa | Elecrow **SX1262 module** in the wireless-module slot, **direct SPI to the P4**: CLK **8** / MISO **7** / MOSI **6**, NSS **10** / BUSY **9** / DIO1(IRQ) **53** / NRST **54** | Elecrow GitHub `bsp_wireless.h` (V1.1/V1.2 examples) |
| LoRa radio cfg | Elecrow example: RadioLib 7.2.1 on P4, SPI @8 MHz, `begin(..., 22 dBm, preamble 8, **TCXO 1.6 V**)`; **no** `setDio2AsRfSwitch` call → `[hypothesis]` DIO2 switch NOT used; verify at RF bring-up (T-Deck issue #6 class if wrong) | Elecrow `bsp_wireless.cpp` |
| USB serial | CH340K UART bridge (VID 1A86:7522). OPEN: which chip the UART routes to (P4 vs C6) — determine from Elecrow schematic before Tier-A esptool assumptions | registry notes |

References: [Elecrow product page](https://www.elecrow.com/crowpanel-advanced-7inch-esp32-p4-hmi-ai-display-1024x600-ips-touch-screen-with-wifi-6-compatible-with-arduino-lvgl-micropython.html)
· [Wiki Lesson14 SX1262](https://www.elecrow.com/wiki/CrowPanel_Advanced_7inch_ESP32-P4_HMI_AI_Display_Lesson14.html)
· [Elecrow-RD GitHub examples](https://github.com/Elecrow-RD/CrowPanel-Advanced-7inch-ESP32-P4-HMI-AI-Display-1024x600-IPS-Touch-Screen)
· DCC repo `firmware/` (P4 display/touch/hosted, production-proven on this desk)
· `TANMATSU_PORT.md` (the P4 arduino-as-component recipe, production since beta_17)

## Architecture decision (2026-07-15)

**Single-repo pioarduino env** — `[env:crowpanel7_companion_radio_touch]` in
`platformio.ini`, mirroring the S3 env structure, NOT a tanmatsu-style IDF subproject.

Why:
1. **Tanmatsu went IDF-subproject ONLY because of its launcher/AppFS app model**
   (TANMATSU_PORT.md "Decision (architecture)"). CrowPanel7 is a bare device — wadamesh
   IS the firmware. The pioarduino route the Tanmatsu plan originally drafted applies
   cleanly here.
2. **Toolchain-proven on this machine:** DCC built its CrowPanel7 firmware here with
   pioarduino (`platform-espressif32` / `board=esp32-p4` / IDF managed components).
   The tanmatsu/ build was developed on macOS (its build.sh uses `sed -i ''`), its
   vendored components are absent locally, and no ESP-IDF is installed on this box.
3. **arduino-esp32 3.x is first-class on P4 since 3.1.3** (Tanmatsu spike proved the
   entire app + MeshCore core + LVGL compiles and links on P4 under arduino-as-component).
   pioarduino `framework = arduino, espidf` gives the same arduino-as-component build
   WITH sdkconfig control (needed for ESP-Hosted SDIO pin map + P4 rev/PSRAM config)
   AND normal PIO `lib_deps` resolution (no vendoring).
4. Radio is **standard RadioLib `CustomSX1262`** (same class as Heltec V4) — Elecrow's
   own example runs RadioLib on the P4 over the slot SPI. No bridge class needed
   (unlike Tanmatsu, whose SX1262 hangs off the C6).

Fallback if the hybrid build fights back: tanmatsu-style IDF subproject `crowpanel7/`
(recipe fully documented in TANMATSU_PORT.md; cost = IDF-on-Windows install + vendoring).

## What each subsystem maps to

| Subsystem | CrowPanel7 implementation | Provenance |
|---|---|---|
| Board | `variants/crowpanel7/CrowPanel7Board.h : ESP32Board` (no battery ADC — mains HMI; PIN_VBAT_READ=-1) | ThinkNode/Tanmatsu pattern |
| Display | `CrowPanel7Display : DisplayDriver` — EK79007 via `esp_lcd_ek79007` managed component + MIPI-DSI init (DCC code adapted); `writePixelsRGB565()` → `esp_lcd_panel_draw_bitmap` | TanmatsuDisplay.h pattern + DCC init |
| Touch | reuse the GT911 driver pattern from `src/helpers/input/TDeckTouch.cpp` (same controller, same 0x5D) with pins 45/46, screen 1024×600; new `HAS_CROWPANEL7_GT911` gate | TDeckTouch |
| Radio | `RADIO_CLASS=CustomSX1262` + slot pins; `SX126X_DIO3_TCXO_VOLTAGE=1.6`; `SX126X_DIO2_AS_RF_SWITCH=false` `[hypothesis — verify on RF bring-up]`; fleet params LORA_FREQ=869.618 BW=62.5 SF=8 | S3 envs + Elecrow example |
| WiFi | arduino `WiFi.h` over `esp_wifi_remote`/`esp_hosted` (C6, SDIO) — sdkconfig from DCC verbatim; expect the hosted `ESP_ERR_NOT_ALLOWED` double-init tolerance patch (tanmatsu build.sh) as a PIO pre-script | DCC + tanmatsu gotchas |
| BLE companion | NimBLE over hosted C6 — Tanmatsu solved this; port its config. If IDF 5.5 NimBLE `ble_gap_read_local_irk` collision appears: same stub patch, as a pre-script | tanmatsu build.sh |
| Storage | FFat partition (S3 pattern) + SD later (slot pins TBD from Elecrow schematic — OPEN) | S3 envs |
| UI | UITask branches on `hor_res` — needs a 1024×600 layout pass (largest new surface; Tanmatsu did 800×480) | UITask |
| Partitions | new 16 MB standalone csv (`variants/crowpanel7/partitions_crowpanel7.csv`) | tanmatsu 16M.csv reference |

## Epic breakdown (feature: CrowPanel7, umbrella epic #2)

- **Epic A — Build scaffolding (#6):** boards/crowpanel7.json + env block + sdkconfig
  defaults + partitions + minimal variant glue → `pio run -e crowpanel7_companion_radio_touch`
  produces a binary. THE gating epic; proves the pioarduino-hybrid route.
- **Epic B — Display + touch (#7):** EK79007 DSI init + LVGL 8.3 flush + GT911 indev +
  UITask 1024×600 layout pass. Compile-verifiable only until flashing is authorized.
- **Epic C — LoRa radio (#8):** CustomSX1262 on slot pins, TCXO/RF-switch config,
  fleet-interop params. Compile + (later) on-air verify vs the Heltec fleet.
- **Epic D — Connectivity (#9):** hosted WiFi + NimBLE companion + TCP/WS transports.
- **Epic E — Integration + release (#10):** storage, boot flow, merged-bin, OTA env
  name, release notes. Ends in the human-gated flash/QA phase.

Dependency chain: A → {B, C, D} → E. Board truth: the GitHub issues.

## Open questions (running list — reviewed with the owner at stopping points)

1. `[decided]` Device name `crowpanel7-dev`; enrolled via #5.
2. `[decided]` Branch base `test-beta36` (carries the beta_36 work staged for this).
3. `[decided]` pioarduino hybrid over IDF-subproject (rationale above).
4. `[hypothesis]` DIO2-as-RF-switch = false per Elecrow example silence — MUST verify
   at RF bring-up (wrong ⇒ ~16 dB TX loss, T-Deck #6 class).
5. OPEN: CH340K UART routes to P4 or C6? (affects flash/monitor path assumptions).
6. OPEN: SD slot pin map (SDMMC slot/pins) — from Elecrow schematic when SD lands.
7. OPEN: CPU freq — DCC pins 360 MHz for rev<3.0; new unit's rev unknown until first
   authorized boot log. Board json stays at 360 conservatively.
8. OPEN: does the C6 ship with an esp-hosted slave FW version compatible with
   esp_hosted ^2.3.0? (DCC's unit did; assume same batch until proven otherwise.)
