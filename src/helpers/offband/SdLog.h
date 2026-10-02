// src/helpers/offband/SdLog.h
//
// Persistent boot/crash logging to the microSD (TF) card — Strycher/wadamesh#19.
//
// WHY: #13's CrashLog RTC ring solved the *timing* problem (the previous boot's
// log replays on the next boot, so monitor-attach latency stops eating evidence).
// It is still volatile (4 KB, lost on a true power cycle) and needs a serial
// monitor attached at some point to read at all. A file on the card is
// persistent, survives power cycles, and is readable by pulling the card — which
// is what makes intermittent faults like #18 (unexplained reboots) tractable.
//
// v1 SCOPE — deliberately narrow, and NOT on any hot path:
//   At boot, whatever CrashLog recovered from the PREVIOUS boot (reset reason +
//   the captured log) is appended to a file on the card. That is the whole
//   post-mortem, persisted, with zero SD writes during normal running.
//   Rationale: crashLogf()/the esp_log hook can be called from any task (and the
//   hook from near-ISR context). Doing FATFS writes there risks deadlock,
//   priority inversion, and multi-ms stalls on the UI/mesh path — i.e. it would
//   cause the very instability we're chasing. Live streaming to SD, if wanted,
//   is a follow-up that must go through a queue + dedicated low-prio task.
//
// HARDWARE (Elecrow V1.2 schematic, socket J5 — traced, not assumed):
//   SCLK -> R88 -> P4 GPIO43 | CMD(DI) -> R89 -> P4 GPIO44 | D0(DO) -> R90 -> P4 GPIO39
//   DATA1/DATA2: NOT connected to the P4  => 1-bit bus ONLY (4-bit impossible).
//   CS tied to GND via R33 => SD mode, not SPI.
//   Card is SDMMC **slot 0**; slot 1 belongs to the ESP32-C6 hosted link
//   (CLK18/CMD19/D0..D3=17/16/15/14) — do NOT confuse the two (that mix-up is
//   what cost this project a session in #12).
//   P4 has SOC_SDMMC_IO_POWER_EXTERNAL, so SD VDD needs an on-chip LDO power
//   handle (channel 4 = the same VO4 rail that feeds the GPIO39-48 bank).

#pragma once
#include <stddef.h>
#include <stdint.h>

namespace offband {

// Mount the card and append the recovered previous-boot log (if CrashLog found
// one) to the log file. Safe to call with NO card inserted: it logs the reason
// and returns false — it must never hang boot (SAFELANE §6). Idempotent.
// Call AFTER crashLogBegin() (which stashes the recovered text) and after the
// board's rails are up.
bool sdLogBegin();

// True once the card is mounted and the log file is usable.
bool sdLogAvailable();

// Append one line to the log file + flush. Intended for deliberate, low-rate
// events (boot markers, faults) — NOT for hot-path logging. No-op if unmounted.
void sdLogf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

// Human-readable mount status for the UI/CLI ("no card", "mounted 29.7 GB", …).
const char* sdLogStatus();

// Stream the log file to Serial for EXTRACTION WITHOUT PULLING THE CARD.
// tail_bytes = 0 dumps the whole file; otherwise only the last tail_bytes
// (post-mortems live at the end, and 115200 baud makes a full dump slow).
// Framed with BEGIN/END markers so a host-side capture can find the payload.
// Driven from the serial CLI: `scripts/pio-flash send crowpanel7-dev log`
// (see MyMesh.cpp). Writing a log nobody can read off the device is not
// observability — #19.
void sdLogDumpSerial(size_t tail_bytes);

}  // namespace offband
