// src/helpers/offband/SdLog.cpp — see SdLog.h for rationale + hardware notes (#19).
#include "SdLog.h"
#include "CrashLog.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#if defined(ARDUINO) && defined(HAS_CROWPANEL7)
  #include <Arduino.h>
  #include <SD_MMC.h>   // #25: mount via the Arduino SD_MMC driver so the SAME
                        // fs::FS object is reusable by the UI chat-history store
                        // (uiDataFsReady). One mount, one API — no competing mount.
#endif

namespace offband {

#if defined(ARDUINO) && defined(HAS_CROWPANEL7)

// --- Board wiring (schematic-verified, socket J5). See SdLog.h. ---
static constexpr int  kSdClk      = 43;
static constexpr int  kSdCmd      = 44;
static constexpr int  kSdD0       = 39;   // DATA1/2 unconnected -> 1-bit only
static constexpr char kMountPoint[] = "/sdcard";
static constexpr char kLogPath[]    = "/sdcard/wadamesh.log";

// 8.3 filename: FATFS long-filename support is not guaranteed in this build, so
// keep the path short rather than silently failing to open (SAFELANE §6).

static bool         s_mounted = false;
static char         s_status[48] = "not started";

bool sdLogAvailable() { return s_mounted; }
const char* sdLogStatus() { return s_status; }

bool sdLogBegin() {
  if (s_mounted) return true;

  // #25: mount the microSD via the Arduino SD_MMC driver — 1-bit slot 0 (DATA1/2
  // unconnected; slot 1 is the C6 hosted link, never touched). The VO4 rail for
  // the GPIO39-48 bank (SD D0 is GPIO39) is already powered by the peripheral
  // manager and CrowPanel7Display, so no explicit on-chip-LDO handling is needed.
  // Mounting via SD_MMC (rather than a raw esp_vfs_fat mount) gives us ONE mount
  // and one fs::FS object: this logger keeps writing /sdcard/wadamesh.log over the
  // same VFS path, and the UI chat-history store reuses &SD_MMC (uiDataFsReady).
  // No card -> SD_MMC.begin returns false; we degrade cleanly (never hang boot).
  SD_MMC.setPins(kSdClk, kSdCmd, kSdD0);
  if (!SD_MMC.begin(kMountPoint, true /*mode1bit*/) || SD_MMC.cardType() == CARD_NONE) {
    snprintf(s_status, sizeof s_status, "no card");
    crashLogf("[SD] SD_MMC mount failed / no card inserted — continuing without card storage");
    SD_MMC.end();
    return false;
  }

  s_mounted = true;
  const uint64_t mb = SD_MMC.cardSize() >> 20;
  snprintf(s_status, sizeof s_status, "mounted %llu MB", (unsigned long long)mb);
  crashLogf("[SD] card mounted: %llu MB (SD_MMC), log -> %s", (unsigned long long)mb, kLogPath);

  // Persist whatever CrashLog recovered from the PREVIOUS boot. This is the
  // whole point of #19: pull the card after an unexplained reboot (#18) and read
  // the post-mortem without a serial monitor ever being attached.
  const char* prev = crashLogPrevBootText();
  FILE* f = fopen(kLogPath, "a");
  if (!f) {
    crashLogf("[SD] fopen(%s) FAILED — card mounted but log not writable", kLogPath);
    return true;   // card is up; caller can still see status
  }
  fprintf(f, "\n===== BOOT @%lu ms | reset=%s =====\n",
          (unsigned long)millis(), resetReasonString(esp_reset_reason()));
  if (prev && prev[0]) {
    fputs("----- recovered PREVIOUS boot log -----\n", f);
    fputs(prev, f);
    fputs("\n----- end previous boot -----\n", f);
  } else {
    fputs("(no previous-boot log recovered: cold power-on)\n", f);
  }
  fflush(f);
  fclose(f);
  return true;
}

void sdLogf(const char* fmt, ...) {
  if (!s_mounted) return;
  FILE* f = fopen(kLogPath, "a");
  if (!f) return;
  char line[240];
  int n = snprintf(line, sizeof line, "[%lu] ", (unsigned long)millis());
  va_list ap; va_start(ap, fmt);
  vsnprintf(line + n, sizeof(line) - n, fmt, ap);
  va_end(ap);
  fputs(line, f);
  fputc('\n', f);
  fflush(f);
  fclose(f);
}

#else   // not CrowPanel7 (or host build): no-ops, so shared code compiles everywhere

bool sdLogBegin()          { return false; }
bool sdLogAvailable()      { return false; }
void sdLogf(const char*, ...) {}
const char* sdLogStatus()  { return "n/a"; }

#endif


void sdLogDumpSerial(size_t tail_bytes) {
#ifdef ARDUINO
    if (!s_mounted) {
        Serial.printf("[SD] no log to dump (%s)\n", s_status);
        return;
    }
    FILE* f = fopen(kLogPath, "r");
    if (!f) {
        Serial.printf("[SD] fopen(%s) for read FAILED\n", kLogPath);
        return;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    long from = 0;
    if (tail_bytes > 0 && size > (long)tail_bytes) from = size - (long)tail_bytes;
    fseek(f, from, SEEK_SET);

    Serial.printf("\n===== SDLOG BEGIN %s (%ld bytes, from %ld) =====\n",
                  kLogPath, size, from);
    // Chunked so we never sit on a big stack buffer, and so a huge file can't
    // monopolise the loop in one go.
    char buf[256];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        Serial.write((const uint8_t*)buf, n);
        // 115200 baud (~11.5 KB/s): let the UART drain rather than overrun it.
        if ((n == sizeof(buf))) delay(1);
    }
    fclose(f);
    Serial.println("\n===== SDLOG END =====");
#else
    (void)tail_bytes;
#endif
}

}  // namespace offband
