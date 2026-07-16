// src/helpers/offband/CrashLog.h
//
// Panic-survival boot-log ring buffer in RTC_NOINIT memory.
//
// PORTED (minimal subset) from Offband/meshcore-firmware
//   src/helpers/wifi_observer/CrashLog.{h,cpp}  (SafeBoot v2 observability)
// into wadamesh for the CrowPanel7 bring-up (Strycher/wadamesh#13). This
// trimmed port keeps the core no-silent-failures capability and drops the
// v6 extras (heartbeat, NVS boot counter, i2cScan, heap stats) — those are
// separate follow-ups if wanted.
//
// WHY THIS EXISTS (SAFELANE §6 "no silent failures"):
//   The serial monitor attaches ~2 s after reset — AFTER boot-time events
//   (display init, ESP-Hosted C6 handshake, radio init) have already
//   scrolled past. That evidence used to vanish. This buffer lives in RTC
//   slow memory (RTC_NOINIT_ATTR), preserved across soft resets (WDT, panic,
//   brownout, ESP.restart()) and cleared only on hard power-on. On the next
//   boot, crashLogBegin() dumps the PREVIOUS boot's captured log to serial —
//   so a monitor attached after the reset sees the whole prior boot verbatim.
//
//   crashLogInstallEspLogHook() (auto-called by begin) routes EVERY ESP-IDF
//   log line (H_API / transport / H_SDIO_DRV / NimBLE / WiFi driver / …)
//   through the ring in addition to serial — so failures in code we don't
//   own are captured too.
//
// Footprint: 4 KB RTC slow memory. Deps: Arduino + esp_attr/esp_system/
// esp_log + freertos only (no Wire, no Preferences).
// Portable to ESP32-P4 (generic RTC_NOINIT + esp_reset_reason).

#pragma once
#include <stddef.h>
#include <stdint.h>

namespace offband {

// Short human-readable name for an esp_reset_reason() value. Safe before
// crashLogBegin(); returns "UNKNOWN" (never nullptr) for unrecognized codes.
const char* resetReasonString(int reason);

// Call as the FIRST thing in setup() after Serial.begin(). If the previous
// boot's RTC buffer survived (magic matches), dumps it to serial under a
// clear banner, then resets the write index so this boot starts fresh. On a
// true cold power-on the magic won't match and nothing is dumped. Also
// installs the esp_log capture hook + a shutdown handler. Idempotent.
void crashLogBegin();

// printf-style logger: writes one line to BOTH the RTC ring (survives soft
// reset) AND serial (live). Auto-prepends "[<millis>] ". Truncates >~240 ch.
// Never drops a line pre-begin — it still hits serial (SAFELANE §6). Critical-
// section guarded for cross-task use.
void crashLogf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

// Dump the current buffer to serial without clearing (on-demand inspection).
void crashLogDump();

// Mark the buffer empty (rewrite magic, reset index); underlying bytes kept.
void crashLogClear();

// Install esp_log_set_vprintf() capture. Auto-called by crashLogBegin().
// Idempotent.
void crashLogInstallEspLogHook();

// Register a shutdown handler that flushes the buffer to serial on any soft
// reset path (panic/WDT/ESP.restart()). Auto-called by crashLogBegin().
// Idempotent.
void crashLogInstallShutdownHandler();

}  // namespace offband
