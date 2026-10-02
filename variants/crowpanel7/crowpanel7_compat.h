#pragma once
// Compatibility shims for Arduino-ESP32 3.x / ESP32-P4 API gaps the MeshCore core
// still expects. Force-included on the CrowPanel7 build (see platformio.ini
// -include). Mirrors variants/tanmatsu/tanmatsu_compat.h (same arduino 3.x gap).
#include <stdint.h>

// adcAttachPin() was removed in Arduino-ESP32 3.x. The core's ESP32Board calls it
// for the VBAT pin; the CrowPanel 7 is a mains-powered HMI with no battery
// (PIN_VBAT_READ=-1), so a no-op is fine.
static inline void adcAttachPin(uint8_t) {}
