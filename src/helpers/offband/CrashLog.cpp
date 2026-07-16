// src/helpers/offband/CrashLog.cpp
//
// RTC_NOINIT-backed boot-log ring buffer. See CrashLog.h for rationale.
// Trimmed faithful port of Offband/meshcore-firmware's wifi_observer/CrashLog
// (SafeBoot v2), core subset only — Strycher/wadamesh#13.

#include "CrashLog.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#ifdef ARDUINO
  #include <Arduino.h>
  #include <esp_attr.h>      // RTC_NOINIT_ATTR
  #include <esp_system.h>    // esp_reset_reason(), esp_register_shutdown_handler()
  #include <esp_log.h>       // esp_log_set_vprintf()
  #include <freertos/FreeRTOS.h>
  #include <freertos/portmacro.h>
#endif

namespace offband {

// ---------------------------------------------------------------------------
// Reset-reason mapping
// ---------------------------------------------------------------------------
const char* resetReasonString(int reason) {
#ifdef ARDUINO
    switch ((esp_reset_reason_t)reason) {
        case ESP_RST_POWERON:    return "POWERON";
        case ESP_RST_EXT:        return "EXT_PIN";
        case ESP_RST_SW:         return "SW_RESET";
        case ESP_RST_PANIC:      return "PANIC";
        case ESP_RST_INT_WDT:    return "INT_WDT";
        case ESP_RST_TASK_WDT:   return "TASK_WDT";
        case ESP_RST_WDT:        return "OTHER_WDT";
        case ESP_RST_DEEPSLEEP:  return "DEEPSLEEP";
        case ESP_RST_BROWNOUT:   return "BROWNOUT";
        case ESP_RST_SDIO:       return "SDIO";
        default:                 return "UNKNOWN";
    }
#else
    (void)reason;
    return "HOST_BUILD";
#endif
}

// ---------------------------------------------------------------------------
// Ring buffer storage (RTC_NOINIT memory): [header 16 B][data]
// ---------------------------------------------------------------------------
static constexpr uint32_t kCrashLogMagic    = 0xCAFEF00DU;
static constexpr size_t   kCrashLogTotal    = 4096;
static constexpr size_t   kCrashLogDataSize = kCrashLogTotal - 16;  // 4080 B
static constexpr size_t   kCrashLogLineMax  = 240;

struct CrashLogHeader {
    uint32_t magic;
    uint32_t write_index;
    uint32_t wrapped;
    uint32_t reserved;
};

#ifdef ARDUINO
// RTC_NOINIT_ATTR: RTC slow memory, NOT zeroed by soft reset — whatever was
// there before the reset survives. That is the whole point.
RTC_NOINIT_ATTR static CrashLogHeader s_header;
RTC_NOINIT_ATTR static char           s_data[kCrashLogDataSize];
static portMUX_TYPE s_log_mux = portMUX_INITIALIZER_UNLOCKED;
#else
static CrashLogHeader s_header;
static char           s_data[kCrashLogDataSize];
#endif

static bool s_begin_called = false;

// Caller MUST hold the critical section.
static void writeToRing(const char* src, size_t n) {
    if (n == 0) return;
    if (n > kCrashLogDataSize) {           // keep only the last window
        src += (n - kCrashLogDataSize);
        n = kCrashLogDataSize;
    }
    size_t wi = s_header.write_index % kCrashLogDataSize;
    size_t first_chunk = kCrashLogDataSize - wi;
    if (first_chunk >= n) {
        memcpy(&s_data[wi], src, n);
        s_header.write_index = (wi + n) % kCrashLogDataSize;
        if (wi + n >= kCrashLogDataSize) s_header.wrapped = 1;
    } else {
        memcpy(&s_data[wi], src, first_chunk);
        memcpy(&s_data[0],  src + first_chunk, n - first_chunk);
        s_header.write_index = n - first_chunk;
        s_header.wrapped = 1;
    }
}

#ifdef ARDUINO
static void dumpRing() {
    size_t start = s_header.wrapped ? s_header.write_index : 0;
    size_t count = s_header.wrapped ? kCrashLogDataSize : s_header.write_index;
    for (size_t i = 0; i < count; ++i) {
        char c = s_data[(start + i) % kCrashLogDataSize];
        if (c == '\0') continue;
        Serial.write(c);
    }
}
#endif

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void crashLogBegin() {
    if (s_begin_called) return;
    s_begin_called = true;

#ifdef ARDUINO
    bool valid = (s_header.magic == kCrashLogMagic) &&
                 (s_header.write_index < kCrashLogDataSize);
    if (valid) {
        Serial.println();
        Serial.println("=========================================================");
        Serial.println("=== BOOT LOG FROM PREVIOUS BOOT (RTC_NOINIT survived) ===");
        Serial.printf("=== reset_reason=%d (%s), buffer wrapped=%u ===\n",
                      (int)esp_reset_reason(),
                      resetReasonString(esp_reset_reason()),
                      (unsigned)s_header.wrapped);
        Serial.println("=========================================================");
        dumpRing();
        Serial.println();
        Serial.println("=========================================================");
        Serial.println("=== END PREVIOUS BOOT (this boot's writes start here) ===");
        Serial.println("=========================================================");
        Serial.println();
    } else {
        Serial.println("[CrashLog] fresh boot; no previous-boot log to recover.");
    }

    // Re-init header for THIS boot. data[] intentionally NOT cleared — the
    // wrap logic handles "empty" (write_index=0, wrapped=0 -> prints nothing).
    portENTER_CRITICAL(&s_log_mux);
    s_header.magic       = kCrashLogMagic;
    s_header.write_index = 0;
    s_header.wrapped     = 0;
    s_header.reserved    = 0;
    portEXIT_CRITICAL(&s_log_mux);

    crashLogInstallEspLogHook();
    crashLogInstallShutdownHandler();
#endif
}

void crashLogf(const char* fmt, ...) {
    char line[kCrashLogLineMax + 1];
    int  prefix_len = 0;

#ifdef ARDUINO
    prefix_len = snprintf(line, sizeof(line), "[%lu] ", (unsigned long)millis());
#else
    prefix_len = snprintf(line, sizeof(line), "[host] ");
#endif
    if (prefix_len < 0 || prefix_len >= (int)sizeof(line)) {
        prefix_len = 0;
        line[0] = '\0';
    }

    va_list ap;
    va_start(ap, fmt);
    int body_len = vsnprintf(line + prefix_len, sizeof(line) - prefix_len, fmt, ap);
    va_end(ap);
    if (body_len < 0) return;

    size_t total = prefix_len + body_len;
    if (total >= sizeof(line)) total = sizeof(line) - 1;
    if (total > 0 && line[total - 1] != '\n' && total < sizeof(line) - 1) {
        line[total++] = '\n';
        line[total]   = '\0';
    }

#ifdef ARDUINO
    // Live path ALWAYS (even pre-begin: the line must not vanish — SAFELANE §6).
    Serial.write((const uint8_t*)line, total);
    if (s_begin_called) {
        portENTER_CRITICAL(&s_log_mux);
        writeToRing(line, total);
        portEXIT_CRITICAL(&s_log_mux);
    }
#else
    fputs(line, stdout);
    writeToRing(line, total);
#endif
}

void crashLogDump() {
#ifdef ARDUINO
    Serial.println("--- crashLogDump (current buffer) ---");
    dumpRing();
    Serial.println("--- end ---");
#endif
}

void crashLogClear() {
#ifdef ARDUINO
    portENTER_CRITICAL(&s_log_mux);
#endif
    s_header.magic       = kCrashLogMagic;
    s_header.write_index = 0;
    s_header.wrapped     = 0;
#ifdef ARDUINO
    portEXIT_CRITICAL(&s_log_mux);
#endif
}

// ---------------------------------------------------------------------------
// ESP-IDF log capture + shutdown handler
// ---------------------------------------------------------------------------
#ifdef ARDUINO

// Recursion guard: a driver may ESP_LOG from inside our handler.
static thread_local bool s_in_vprintf = false;

static int crashlog_vprintf(const char* fmt, va_list ap) {
    if (s_in_vprintf) return 0;
    s_in_vprintf = true;

    char line[256];
    int n = vsnprintf(line, sizeof(line), fmt, ap);
    if (n < 0) { s_in_vprintf = false; return 0; }
    size_t len = (size_t)n;
    if (len >= sizeof(line)) len = sizeof(line) - 1;

    Serial.write((const uint8_t*)line, len);   // live
    if (s_begin_called) {
        portENTER_CRITICAL(&s_log_mux);
        writeToRing(line, len);                // survivable
        portEXIT_CRITICAL(&s_log_mux);
    }
    s_in_vprintf = false;
    return n;
}

void crashLogInstallEspLogHook() {
    static bool installed = false;
    if (installed) return;
    installed = true;
    esp_log_set_vprintf(crashlog_vprintf);   // replace (don't chain)
    esp_log_level_set("*", ESP_LOG_INFO);
    crashLogf("[CrashLog] ESP-IDF log capture installed");
}

static void crashlog_shutdown_handler(void) {
    // Last-gasp dump before the reset finalizes (panic/WDT/ESP.restart()).
    Serial.println();
    Serial.println("=== CRASHLOG SHUTDOWN HANDLER FIRED ===");
    crashLogDump();
    Serial.println("=== END SHUTDOWN DUMP ===");
    Serial.flush();
}

void crashLogInstallShutdownHandler() {
    static bool installed = false;
    if (installed) return;
    installed = true;
    esp_err_t err = esp_register_shutdown_handler(crashlog_shutdown_handler);
    if (err != ESP_OK) {
        crashLogf("[CrashLog] esp_register_shutdown_handler failed: %d", (int)err);
    } else {
        crashLogf("[CrashLog] shutdown handler registered");
    }
}

#else  // host build stubs
void crashLogInstallEspLogHook() {}
void crashLogInstallShutdownHandler() {}
#endif

}  // namespace offband
