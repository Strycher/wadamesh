// src/helpers/offband/SdLog.cpp — see SdLog.h for rationale + hardware notes (#19).
#include "SdLog.h"
#include "CrashLog.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#if defined(ARDUINO) && defined(HAS_CROWPANEL7)
  #include <Arduino.h>
  #include <driver/sdmmc_host.h>
  #include <sd_pwr_ctrl_by_on_chip_ldo.h>
  #include <esp_vfs_fat.h>
  #include <sdmmc_cmd.h>
#endif

namespace offband {

#if defined(ARDUINO) && defined(HAS_CROWPANEL7)

// --- Board wiring (schematic-verified, socket J5). See SdLog.h. ---
static constexpr int  kSdClk      = 43;
static constexpr int  kSdCmd      = 44;
static constexpr int  kSdD0       = 39;
static constexpr int  kSdWidth    = 1;   // DATA1/2 unconnected -> 1-bit only
static constexpr int  kSdLdoChan  = 4;   // P4 SOC_SDMMC_IO_POWER_EXTERNAL -> on-chip LDO VO4
static constexpr char kMountPoint[] = "/sdcard";
static constexpr char kLogPath[]    = "/sdcard/wadamesh.log";

// 8.3 filename: FATFS long-filename support is not guaranteed in this build, so
// keep the path short rather than silently failing to open (SAFELANE §6).

static bool         s_mounted = false;
static sdmmc_card_t* s_card   = nullptr;
static char         s_status[48] = "not started";

bool sdLogAvailable() { return s_mounted; }
const char* sdLogStatus() { return s_status; }

bool sdLogBegin() {
  if (s_mounted) return true;

  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.slot = SDMMC_HOST_SLOT_0;   // slot 1 is the C6 hosted link — never touch it

  // SD VDD on the P4 rides the on-chip LDO (SOC_SDMMC_IO_POWER_EXTERNAL). Try to
  // take our own power handle, but do NOT treat failure as fatal:
  // arduino's peripheral manager already auto-acquires VO4 at 3300 mV for the
  // GPIO39-48 bank (BOARD_PERIMAN_IO_LDO_AUTO in the esp32p4 variant) — and our
  // SD D0 *is* GPIO39 — so the rail is already up and the channel is taken.
  // Observed exactly that on first bring-up:
  //   ldo: can't acquire the channel, already in use by others or not adjustable
  //   sd_ldo: failed to enable the on-chip LDO unit   -> 0x102
  // In that case mount WITHOUT a pwr_ctrl_handle and let the card use the rail
  // periman already powers. (CrowPanel7Display also acquires VO4 for the touch
  // bus, so the rail is held for the life of the app either way.)
  sd_pwr_ctrl_ldo_config_t ldo_cfg = {};
  ldo_cfg.ldo_chan_id = kSdLdoChan;
  sd_pwr_ctrl_handle_t pwr = nullptr;
  esp_err_t err = sd_pwr_ctrl_new_on_chip_ldo(&ldo_cfg, &pwr);
  if (err == ESP_OK) {
    host.pwr_ctrl_handle = pwr;
  } else {
    pwr = nullptr;
    crashLogf("[SD] on-chip LDO chan %d unavailable (0x%x) — expected: periman already "
              "holds VO4 for GPIO39-48; mounting on the existing rail", kSdLdoChan, err);
  }

  sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
  slot.width = kSdWidth;
  slot.clk   = (gpio_num_t)kSdClk;
  slot.cmd   = (gpio_num_t)kSdCmd;
  slot.d0    = (gpio_num_t)kSdD0;
  slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

  esp_vfs_fat_sdmmc_mount_config_t mcfg = {};
  mcfg.format_if_mount_failed = false;   // NEVER silently wipe a card
  mcfg.max_files              = 3;
  mcfg.allocation_unit_size   = 16 * 1024;

  // NOTE: no ESP_ERROR_CHECK here on purpose. With no card this returns
  // ESP_ERR_TIMEOUT after the card-init retries; the Espressif example aborts
  // (reboots) on that, which would brick boot for anyone without a card.
  err = esp_vfs_fat_sdmmc_mount(kMountPoint, &host, &slot, &mcfg, &s_card);
  if (err != ESP_OK) {
    snprintf(s_status, sizeof s_status,
             (err == ESP_ERR_TIMEOUT) ? "no card" : "mount fail 0x%x", err);
    crashLogf("[SD] mount failed: %s (0x%x) — continuing without card logging",
              (err == ESP_ERR_TIMEOUT) ? "no card inserted" : "error", err);
    if (pwr) sd_pwr_ctrl_del_on_chip_ldo(pwr);
    return false;
  }

  s_mounted = true;
  const uint64_t mb = s_card ? ((uint64_t)s_card->csd.capacity * s_card->csd.sector_size) >> 20 : 0;
  snprintf(s_status, sizeof s_status, "mounted %llu MB", (unsigned long long)mb);
  crashLogf("[SD] card mounted: %llu MB, log -> %s", (unsigned long long)mb, kLogPath);

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

}  // namespace offband
