#pragma once

// Boot-phase trace hook (defined in src/main.cpp); target.cpp stamps phase 1
// from a global ctor, before setup(). Same pattern as the Heltec V4 variant.
extern "C" void set_boot_phase(int phase);

// CrowPanel Advance 7 (ESP32-P4) target wiring. Radio is a standard RadioLib
// CustomSX1262 on the Elecrow module-slot SPI — same class as the Heltec V4 /
// T-Deck, NOT a coprocessor bridge (unlike the Tanmatsu, whose SX1262 hangs
// off the C6). Pins come from platformio.ini P_LORA_* build flags, verified
// against Elecrow-RD's bsp_wireless.h example.

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/AutoDiscoverRTCClock.h>
#include "../../src/helpers/ClockFloorRTC.h"   // monotonic send-timestamp floor (issue #89)
#include "CrowPanel7Board.h"
#ifdef DISPLAY_CLASS
  #include "CrowPanel7Display.h"
  #include <helpers/ui/MomentaryButton.h>
#endif
#include "helpers/sensors/EnvironmentSensorManager.h"

extern CrowPanel7Board board;
extern WRAPPER_CLASS radio_driver;
extern RADIO_CLASS radio;
extern ClockFloorRTC rtc_clock;
extern EnvironmentSensorManager sensors;

#ifdef DISPLAY_CLASS
  extern DISPLAY_CLASS display;
  extern MomentaryButton user_btn;
#endif

bool radio_init();
mesh::LocalIdentity radio_new_identity();
