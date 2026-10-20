#include "storage/TfExposeInternal.h"

#include <SD_MMC.h>
#include <string.h>

#include "USBMSC.h"
#include "display/Display.h"
#include "driver/sdmmc_host.h"
#include "hid/UsbHid.h"
#include "sdmmc_cmd.h"
#include "Branding.h"
#include "board_pins.h"
#include "storage/ConfigStore.h"
#include "storage/TfExpose.h"
#include "storage/UsbConsole.h"
#include "tusb.h"

extern "C" {
#include "diskio_impl.h"
#include "diskio_sdmmc.h"
#include "ff.h"
DRESULT ff_disk_read(BYTE pdrv, BYTE* buff, DWORD sector, UINT count);
DRESULT ff_disk_write(BYTE pdrv, const BYTE* buff, DWORD sector, UINT count);
DRESULT ff_disk_ioctl(BYTE pdrv, BYTE cmd, void* buff);
}

namespace storage {
namespace tf_internal {

USBMSC g_msc;

constexpr BYTE kCtrlSync = 0;
constexpr BYTE kAttrVolumeId = 0x08;

const char* configuredVolumeLabel() {
  return storage::ConfigStore::instance().volumeLabel();
}

// Create one directory entry (file-in-the-way → remove, then mkdir).
bool ensureDirEntry(const char* path) {
  if (SD_MMC.exists(path)) {
    File f = SD_MMC.open(path);
    const bool is_dir = f && f.isDirectory();
    if (f) {
      f.close();
    }
    if (is_dir) {
      return true;
    }
    SD_MMC.remove(path);
  }
  if (!SD_MMC.mkdir(path)) {
    PoSer().printf("TF: mkdir %s failed\n", path);
    return false;
  }
  return true;
}

// Create full tree: "/Audio/Audio Tests" → "/Audio" then "/Audio/Audio Tests".
bool ensureDirTree(const char* path) {
  if (!path || path[0] != '/') {
    PoSer().printf("TF: bad folder path '%s'\n", path ? path : "(null)");
    return false;
  }

  char buf[kPathBuf];
  size_t n = 0;
  buf[n++] = '/';

  for (const char* p = path + 1;; ++p) {
    if (*p == '/' || *p == '\0') {
      if (n > 1) {
        buf[n] = '\0';
        if (!ensureDirEntry(buf)) {
          return false;
        }
      }
      if (*p == '\0') {
        break;
      }
      if (n + 1 >= sizeof(buf)) {
        PoSer().printf("TF: path too long: %s\n", path);
        return false;
      }
      buf[n++] = '/';
    } else {
      if (n + 1 >= sizeof(buf)) {
        PoSer().printf("TF: path too long: %s\n", path);
        return false;
      }
      buf[n++] = *p;
    }
  }
  return true;
}

void makeFatLabel(const char* label, char out[11]) {
  memset(out, ' ', 11);
  size_t n = 0;
  for (const char* p = label; *p && n < 11; ++p) {
    char c = *p;
    if (c >= 'a' && c <= 'z') {
      c = static_cast<char>(c - 'a' + 'A');
    }
    if (c == ' ' || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-') {
      out[n++] = c;
    }
  }
}

// FatFS label API is off (FF_USE_LABEL=0).
// Ask FatFS where root lives (dirbase/database), then patch BPB + root VOL_ID.
// Must run while ff_diskio pdrv 0 is registered and nothing else has the volume mounted.
bool writeVolumeLabel(const char* label) {
  FATFS fs;
  memset(&fs, 0, sizeof(fs));
  const FRESULT fr = f_mount(&fs, "0:", 1);
  if (fr != FR_OK) {
    PoSer().printf("TF: label mount fail %d\n", static_cast<int>(fr));
    return false;
  }

  const DWORD volbase = fs.volbase;
  const DWORD database = fs.database;
  const DWORD csize = fs.csize ? fs.csize : 1;
  const BYTE fs_type = fs.fs_type;
  DWORD root_sec = 0;
  if (fs_type == FS_FAT32) {
    // dirbase is root cluster number
    const DWORD root_cl = fs.dirbase >= 2 ? fs.dirbase : 2;
    root_sec = database + (root_cl - 2) * csize;
  } else if (fs_type == FS_FAT16 || fs_type == FS_FAT12) {
    root_sec = fs.dirbase;  // absolute sector
  } else {
    f_mount(nullptr, "0:", 0);
    PoSer().printf("TF: label fail — fs_type %u\n", static_cast<unsigned>(fs_type));
    return false;
  }

  // Drop FatFS so its sector cache cannot stomp our raw writes.
  f_mount(nullptr, "0:", 0);

  char lab[11];
  makeFatLabel(label, lab);  // exactly 11 bytes, space-padded (macOS Sequoia needs this)

  BYTE bpb[512];
  if (ff_disk_read(0, bpb, volbase, 1) != RES_OK) {
    PoSer().println("TF: label fail — VBR read");
    return false;
  }

  const bool fat32 = (fs_type == FS_FAT32);
  const int bpb_lab_off = fat32 ? 71 : 43;
  memcpy(bpb + bpb_lab_off, lab, 11);
  if (fat32) {
    bpb[66] = 0x29;  // BS_BootSig
  } else {
    bpb[38] = 0x29;
  }
  if (ff_disk_write(0, bpb, volbase, 1) != RES_OK) {
    PoSer().println("TF: label fail — VBR write");
    return false;
  }
  if (fat32) {
    const uint16_t bk = static_cast<uint16_t>(bpb[50] | (bpb[51] << 8));
    ff_disk_write(0, bpb, volbase + (bk ? bk : 6), 1);
  }

  BYTE dir[512];
  if (ff_disk_read(0, dir, root_sec, 1) != RES_OK) {
    PoSer().printf("TF: label fail — root read LBA %lu\n", static_cast<unsigned long>(root_sec));
    return false;
  }

  int slot = -1;
  for (int i = 0; i < 512; i += 32) {
    const BYTE first = dir[i];
    const BYTE attr = dir[i + 11];
    if (first != 0x00 && first != 0xE5 && attr == kAttrVolumeId) {
      slot = i;
      break;
    }
    if (slot < 0 && (first == 0x00 || first == 0xE5)) {
      slot = i;
      if (first == 0x00) {
        break;
      }
    }
  }
  if (slot < 0) {
    PoSer().println("TF: label fail — no root slot");
    return false;
  }

  memset(dir + slot, 0, 32);
  memcpy(dir + slot, lab, 11);
  dir[slot + 11] = kAttrVolumeId;
  if (ff_disk_write(0, dir, root_sec, 1) != RES_OK) {
    PoSer().println("TF: label fail — root write");
    return false;
  }
  ff_disk_ioctl(0, kCtrlSync, nullptr);

  // Read-back proof (what the Mac will see).
  BYTE chk_bpb[512];
  BYTE chk_dir[512];
  const bool bpb_ok =
      ff_disk_read(0, chk_bpb, volbase, 1) == RES_OK && memcmp(chk_bpb + bpb_lab_off, lab, 11) == 0;
  const bool root_ok =
      ff_disk_read(0, chk_dir, root_sec, 1) == RES_OK && memcmp(chk_dir + slot, lab, 11) == 0 &&
      chk_dir[slot + 11] == kAttrVolumeId;

  PoSer().printf("TF: volume label → '%.11s' vol=%lu root=%lu slot=%d bpb=%s root=%s\n", lab,
                static_cast<unsigned long>(volbase), static_cast<unsigned long>(root_sec), slot,
                bpb_ok ? "ok" : "BAD", root_ok ? "ok" : "BAD");
  return bpb_ok && root_ok;
}

bool setSdPins(bool mode1bit) {
  // Pin order: clk, cmd, d0[, d1, d2, d3] — from config/board.ini (compile-time)
  bool ok = false;
  if (mode1bit) {
    ok = SD_MMC.setPins(pins::SD_CLK, pins::SD_CMD, pins::SD_D0);
  } else {
    ok = SD_MMC.setPins(pins::SD_CLK, pins::SD_CMD, pins::SD_D0, pins::SD_D1, pins::SD_D2,
                        pins::SD_D3);
  }
  if (!ok) {
    PoSer().printf("TF: setPins %s-bit failed\n", mode1bit ? "1" : "4");
  }
  return ok;
}

uint32_t sectorCount() {
  const uint64_t bytes = SD_MMC.cardSize();
  if (bytes == 0) {
    return 0;
  }
  return static_cast<uint32_t>(bytes / 512ULL);
}

int32_t onRead(uint32_t lba, uint32_t /*offset*/, void* buffer, uint32_t bufsize) {
  if (!storage::TfExpose::instance().enabled() || !buffer || bufsize < 512) {
    return -1;
  }
  const uint32_t count = bufsize / 512;
  if (count == 0) {
    return -1;
  }
  if (ff_disk_read(0, static_cast<BYTE*>(buffer), lba, count) != RES_OK) {
    return -1;
  }
  storage::TfExpose::instance().noteActivity();
  return static_cast<int32_t>(bufsize);
}

int32_t onWrite(uint32_t lba, uint32_t /*offset*/, uint8_t* buffer, uint32_t bufsize) {
  if (!storage::TfExpose::instance().enabled() || !buffer || bufsize < 512) {
    return -1;
  }
  const uint32_t count = bufsize / 512;
  if (count == 0) {
    return -1;
  }
  if (ff_disk_write(0, buffer, lba, count) != RES_OK) {
    return -1;
  }
  storage::TfExpose::instance().noteActivity();
  return static_cast<int32_t>(bufsize);
}

bool onStartStop(uint8_t /*power*/, bool start, bool load_eject) {
  // macOS Finder eject: LoEj=1, Start=0 — stop offering the volume.
  if (load_eject && !start) {
    g_msc.mediaPresent(false);
    storage::TfExpose::instance().noteHostEject();
  }
  return true;
}

bool tryBegin(bool mode1bit, int freq_khz, bool format_if_failed) {
  SD_MMC.end();  // clear any half-init state
  delay(20);
  if (!setSdPins(mode1bit)) {
    return false;
  }
  PoSer().printf("TF: SD_MMC.begin %s-bit freq=%d format=%d\n", mode1bit ? "1" : "4", freq_khz,
                format_if_failed ? 1 : 0);
  if (!SD_MMC.begin("/sdcard", mode1bit, format_if_failed, freq_khz, 5)) {
    PoSer().println("TF: begin failed");
    SD_MMC.end();
    return false;
  }
  const auto type = SD_MMC.cardType();
  if (type == CARD_NONE) {
    PoSer().println("TF: CARD_NONE after begin");
    SD_MMC.end();
    return false;
  }
  PoSer().printf("TF: OK type=%u size=%lluMB\n", static_cast<unsigned>(type),
                SD_MMC.cardSize() / (1024ULL * 1024ULL));
  return true;
}

// Open card on pdrv 0 (no VFS). Caller must rawDiskClose().
sdmmc_card_t* rawDiskOpen() {
  SD_MMC.end();
  delay(30);
  sdmmc_host_deinit();

  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.flags = SDMMC_HOST_FLAG_1BIT;
  host.slot = SDMMC_HOST_SLOT_1;
  host.max_freq_khz = 400;

  sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
  slot.width = 1;
#ifdef SOC_SDMMC_USE_GPIO_MATRIX
  slot.clk = static_cast<gpio_num_t>(pins::SD_CLK);
  slot.cmd = static_cast<gpio_num_t>(pins::SD_CMD);
  slot.d0 = static_cast<gpio_num_t>(pins::SD_D0);
  slot.d1 = GPIO_NUM_NC;
  slot.d2 = GPIO_NUM_NC;
  slot.d3 = GPIO_NUM_NC;
#endif

  esp_err_t err = sdmmc_host_init();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    PoSer().printf("TF: host_init %s\n", esp_err_to_name(err));
    return nullptr;
  }
  err = sdmmc_host_init_slot(host.slot, &slot);
  if (err != ESP_OK) {
    PoSer().printf("TF: slot_init %s\n", esp_err_to_name(err));
    sdmmc_host_deinit();
    return nullptr;
  }

  sdmmc_card_t* card = static_cast<sdmmc_card_t*>(calloc(1, sizeof(sdmmc_card_t)));
  if (!card) {
    sdmmc_host_deinit();
    return nullptr;
  }
  err = sdmmc_card_init(&host, card);
  if (err != ESP_OK) {
    PoSer().printf("TF: card_init %s\n", esp_err_to_name(err));
    free(card);
    sdmmc_host_deinit();
    return nullptr;
  }
  ff_diskio_register_sdmmc(0, card);
  return card;
}

void rawDiskClose(sdmmc_card_t* card) {
  ff_diskio_unregister(0);
  free(card);
  sdmmc_host_deinit();
}

bool rawApplyVolumeLabel() {
  sdmmc_card_t* card = rawDiskOpen();
  if (!card) {
    return false;
  }
  const bool ok = writeVolumeLabel(configuredVolumeLabel());
  rawDiskClose(card);
  return ok;
}

// Wipe any filesystem (exFAT/corrupt) by talking to the card without VFS mount.
// begin(..., format_if_failed) hangs on exFAT — this path does not.
bool rawFormatFat32(void (*pct)(int, const char*)) {
  auto tick = [&](int p, const char* log) {
    PoSer().println(log);
    Serial.flush();
    if (pct) {
      pct(p, log);
    }
  };

  tick(25, "TF: card init");
  sdmmc_card_t* card = rawDiskOpen();
  if (!card) {
    return false;
  }

  tick(40, "TF: mkfs FAT32");
  constexpr size_t kWork = 4096;
  BYTE* work = static_cast<BYTE*>(malloc(kWork));
  FRESULT res = FR_NOT_ENOUGH_CORE;
  bool labeled = false;
  if (work) {
    // FM_SFD = no MBR — volume starts at LBA 0 (Mac sees our label).
    tick(55, "TF: writing FS");
    res = f_mkfs("0:", FM_FAT32 | FM_SFD, 0, work, kWork);
    if (res != FR_OK) {
      PoSer().printf("TF: FAT32|SFD mkfs %d, trying ANY|SFD\n", static_cast<int>(res));
      tick(60, "TF: mkfs retry");
      res = f_mkfs("0:", FM_ANY | FM_SFD, 0, work, kWork);
    }
    if (res == FR_OK) {
      tick(75, "TF: label");
      labeled = writeVolumeLabel(configuredVolumeLabel());
    }
    free(work);
  }

  rawDiskClose(card);
  PoSer().printf("TF: raw mkfs result %d label=%d\n", static_cast<int>(res), labeled ? 1 : 0);
  return res == FR_OK && labeled;
}



}  // namespace tf_internal
}  // namespace storage
