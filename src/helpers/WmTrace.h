#pragma once
// WmTrace — DEBUG_LEVEL-gated execution tracing for wadamesh.  (#61)
//
// WHY THIS EXISTS
// ---------------
// Diagnosing the V4-R8 idle wedge took two days and ~10 flashes because every
// probe was written after the fact and thrown away afterwards:
//   * stallLog() / STALLMAX / LVGLSPLIT all report AFTER the measured call
//     returns.  A thread that never returns prints NOTHING, so a hang looks
//     exactly like a healthy idle device.  Silence was repeatedly misread as
//     health.
//   * each round of narrowing needed a new one-off Serial.printf, a rebuild and
//     a flash, and several of those probes were themselves wrong (one was
//     placed inside `#if CAP_TRACKBALL` and compiled out on this board; one
//     never reset its state so a stale value looked like a live reading).
//
// The fix is the ordinary discipline: instrument entry, exit, the result of
// each interesting call, and each step between hazards — verbosely — ONCE, and
// gate it behind a level so it can be switched on when needed and costs
// nothing when off.
//
// TWO MECHANISMS, deliberately
// ----------------------------
//   1. PRINTS (WM_ENTER/WM_EXIT/WM_STEP/WM_CALL) — the normal trace.  Only
//      useful for code that actually returns.
//   2. BREADCRUMB (wmTraceWhere()) — a position marker updated BEFORE each
//      step and readable from ANOTHER TASK.  This is the part that survives a
//      wedge: the heartbeat on core 0 prints where the wedged thread is while
//      it is still stuck.  Any tracing scheme that only prints on completion
//      cannot diagnose a hang, which is the whole lesson of #61.
//
// LEVELS  (-D WM_TRACE_LEVEL=n)
//   0  off — macros compile to nothing; breadcrumb still updates (1 store, free)
//   1  key transitions only (screen/CPU/input/state changes)
//   2  + function entry/exit for instrumented functions
//   3  + every step and call result (very verbose)
//
// Breadcrumbs are ALWAYS live, even at level 0: a single volatile byte store is
// cheaper than the bug it prevents, and it means a field unit can report where
// it hung without a special build.

#include <Arduino.h>

#ifndef WM_TRACE_LEVEL
  #define WM_TRACE_LEVEL 0
#endif

// ---- breadcrumb ------------------------------------------------------------
// Set before each step; read by the heartbeat task on the other core.  Static
// strings only (no formatting, no allocation) so it is safe to set anywhere,
// including immediately before a call that may never return.
extern volatile const char* g_wm_where;
extern volatile uint32_t    g_wm_where_ms;

static inline void wmMark(const char* where) {
  g_wm_where    = where;
  g_wm_where_ms = millis();
}
// Where is the traced thread now, and for how long?  Used by the heartbeat.
static inline const char* wmTraceWhere() {
  return g_wm_where ? (const char*)g_wm_where : "(none)";
}
static inline uint32_t wmTraceStuckMs() {
  return millis() - g_wm_where_ms;
}

// ---- printing --------------------------------------------------------------
#if WM_TRACE_LEVEL > 0
  #define WM_LOG(lvl, fmt, ...)                                                \
      do { if ((lvl) <= WM_TRACE_LEVEL)                                        \
             Serial.printf("[t=%lu] " fmt "\n",                                \
                           (unsigned long)millis(), ##__VA_ARGS__); } while (0)
#else
  #define WM_LOG(lvl, fmt, ...) do {} while (0)
#endif

// Entry/exit.  WM_ENTER also drops a breadcrumb, so even at level 0 the
// heartbeat can name the function a wedged thread is inside.
#define WM_ENTER(name)      do { wmMark(name); WM_LOG(2, "[>] %s", name); } while (0)
#define WM_EXIT(name)       do { WM_LOG(2, "[<] %s", name); wmMark("(idle)"); } while (0)

// A step between hazards.  Breadcrumb first, print second — if the next
// statement hangs, the breadcrumb has already recorded where.
#define WM_STEP(name)       do { wmMark(name); WM_LOG(3, "[.] %s", name); } while (0)

// A call whose RESULT matters.  Marks, invokes, then reports the value.
#define WM_CALL_I(name, expr)                                                  \
    ([&]{ wmMark(name); auto _r = (expr);                                      \
          WM_LOG(3, "[=] %s -> %ld", name, (long)_r); return _r; }())
#define WM_CALL_B(name, expr)                                                  \
    ([&]{ wmMark(name); bool _r = (expr);                                      \
          WM_LOG(3, "[=] %s -> %s", name, _r ? "true" : "false"); return _r; }())

// A void call that may block.  Breadcrumb before, so a hang is attributable.
#define WM_VOID(name, expr) do { wmMark(name); expr; WM_LOG(3, "[=] %s done", name); } while (0)
