#include "storage/TfExpose.h"

#include <Preferences.h>
#include <SD_MMC.h>
#include <string.h>

#include "USBMSC.h"
#include "board_pins.h"
#include "display/Display.h"
#include "driver/sdmmc_host.h"
#include "hid/UsbHid.h"
#include "sdmmc_cmd.h"
#include "tusb.h"

extern "C" {
#include "diskio_impl.h"
#include "diskio_sdmmc.h"
#include "ff.h"
// Real symbols (ffconf macros rename disk_* → ff_disk_*).
DRESULT ff_disk_read(BYTE pdrv, BYTE* buff, DWORD sector, UINT count);
DRESULT ff_disk_write(BYTE pdrv, const BYTE* buff, DWORD sector, UINT count);
DRESULT ff_disk_ioctl(BYTE pdrv, BYTE cmd, void* buff);
}

namespace {

USBMSC g_msc;

const char* kDirs[] = {"/Mac", "/PC", "/Linux", "/Logs", "/Audio"};
constexpr const char* kVolumeLabel = "PocketOps";  // host drive name
constexpr BYTE kCtrlSync = 0;  // CTRL_SYNC
constexpr BYTE kAttrVolumeId = 0x08;

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
    Serial.printf("TF: label mount fail %d\n", static_cast<int>(fr));
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
    Serial.printf("TF: label fail — fs_type %u\n", static_cast<unsigned>(fs_type));
    return false;
  }

  // Drop FatFS so its sector cache cannot stomp our raw writes.
  f_mount(nullptr, "0:", 0);

  char lab[11];
  makeFatLabel(label, lab);  // exactly 11 bytes, space-padded (macOS Sequoia needs this)

  BYTE bpb[512];
  if (ff_disk_read(0, bpb, volbase, 1) != RES_OK) {
    Serial.println("TF: label fail — VBR read");
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
    Serial.println("TF: label fail — VBR write");
    return false;
  }
  if (fat32) {
    const uint16_t bk = static_cast<uint16_t>(bpb[50] | (bpb[51] << 8));
    ff_disk_write(0, bpb, volbase + (bk ? bk : 6), 1);
  }

  BYTE dir[512];
  if (ff_disk_read(0, dir, root_sec, 1) != RES_OK) {
    Serial.printf("TF: label fail — root read LBA %lu\n", static_cast<unsigned long>(root_sec));
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
    Serial.println("TF: label fail — no root slot");
    return false;
  }

  memset(dir + slot, 0, 32);
  memcpy(dir + slot, lab, 11);
  dir[slot + 11] = kAttrVolumeId;
  if (ff_disk_write(0, dir, root_sec, 1) != RES_OK) {
    Serial.println("TF: label fail — root write");
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

  Serial.printf("TF: volume label → '%.11s' vol=%lu root=%lu slot=%d bpb=%s root=%s\n", lab,
                static_cast<unsigned long>(volbase), static_cast<unsigned long>(root_sec), slot,
                bpb_ok ? "ok" : "BAD", root_ok ? "ok" : "BAD");
  return bpb_ok && root_ok;
}

bool setSdPins(bool mode1bit) {
  // Exact LilyGO T-Dongle-S3 pin order: clk, cmd, d0[, d1, d2, d3]
  bool ok = false;
  if (mode1bit) {
    ok = SD_MMC.setPins(pins::SD_CLK, pins::SD_CMD, pins::SD_D0);
  } else {
    ok = SD_MMC.setPins(pins::SD_CLK, pins::SD_CMD, pins::SD_D0, pins::SD_D1, pins::SD_D2,
                        pins::SD_D3);
  }
  if (!ok) {
    Serial.printf("TF: setPins %s-bit failed\n", mode1bit ? "1" : "4");
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

bool onStartStop(uint8_t /*power*/, bool /*start*/, bool /*eject*/) { return true; }

bool tryBegin(bool mode1bit, int freq_khz, bool format_if_failed = false) {
  SD_MMC.end();  // clear any half-init state
  delay(20);
  if (!setSdPins(mode1bit)) {
    return false;
  }
  Serial.printf("TF: SD_MMC.begin %s-bit freq=%d format=%d\n", mode1bit ? "1" : "4", freq_khz,
                format_if_failed ? 1 : 0);
  if (!SD_MMC.begin("/sdcard", mode1bit, format_if_failed, freq_khz, 5)) {
    Serial.println("TF: begin failed");
    SD_MMC.end();
    return false;
  }
  const auto type = SD_MMC.cardType();
  if (type == CARD_NONE) {
    Serial.println("TF: CARD_NONE after begin");
    SD_MMC.end();
    return false;
  }
  Serial.printf("TF: OK type=%u size=%lluMB\n", static_cast<unsigned>(type),
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
    Serial.printf("TF: host_init %s\n", esp_err_to_name(err));
    return nullptr;
  }
  err = sdmmc_host_init_slot(host.slot, &slot);
  if (err != ESP_OK) {
    Serial.printf("TF: slot_init %s\n", esp_err_to_name(err));
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
    Serial.printf("TF: card_init %s\n", esp_err_to_name(err));
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
  const bool ok = writeVolumeLabel(kVolumeLabel);
  rawDiskClose(card);
  return ok;
}

// Wipe any filesystem (exFAT/corrupt) by talking to the card without VFS mount.
// begin(..., format_if_failed) hangs on exFAT — this path does not.
bool rawFormatFat32(void (*pct)(int, const char*)) {
  auto tick = [&](int p, const char* log) {
    Serial.println(log);
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
      Serial.printf("TF: FAT32|SFD mkfs %d, trying ANY|SFD\n", static_cast<int>(res));
      tick(60, "TF: mkfs retry");
      res = f_mkfs("0:", FM_ANY | FM_SFD, 0, work, kWork);
    }
    if (res == FR_OK) {
      tick(75, "TF: label");
      labeled = writeVolumeLabel(kVolumeLabel);
    }
    free(work);
  }

  rawDiskClose(card);
  Serial.printf("TF: raw mkfs result %d label=%d\n", static_cast<int>(res), labeled ? 1 : 0);
  return res == FR_OK && labeled;
}

}  // namespace

namespace storage {

TfExpose& TfExpose::instance() {
  static TfExpose s;
  return s;
}

void TfExpose::noteActivity() { activity_ms_ = millis(); }

bool TfExpose::activityRecent(uint32_t window_ms) const {
  if (activity_ms_ == 0) {
    return false;
  }
  return (millis() - activity_ms_) < window_ms;
}

void TfExpose::persist(bool on) {
  Preferences prefs;
  if (!prefs.begin("pocketops", false)) {
    return;
  }
  prefs.putBool("tf_on", on);
  prefs.end();
}

void TfExpose::loadSaved() {
  delay(300);
  refreshCardPresent();

  Preferences prefs;
  bool want = false;
  if (prefs.begin("pocketops", true)) {
    want = prefs.getBool("tf_on", false);
    prefs.end();
  }
  if (want) {
    enable();
  }
}

bool TfExpose::refreshCardPresent() {
  if (mounted_) {
    probe_ok_ = true;
    probed_ = true;
    return true;
  }

  if (!mountCard()) {
    probe_ok_ = false;
    probed_ = true;
    return false;
  }
  if (!enabled_) {
    unmountCard();
  }
  probe_ok_ = true;
  probed_ = true;
  return true;
}

void TfExpose::fillInfo(TfCardInfo& out) {
  out = {};
  out.present = mounted_ && SD_MMC.cardType() != CARD_NONE;
  if (!out.present) {
    return;
  }

  out.type = static_cast<uint8_t>(SD_MMC.cardType());
  switch (SD_MMC.cardType()) {
    case CARD_MMC:
      strncpy(out.type_name, "MMC", sizeof(out.type_name) - 1);
      break;
    case CARD_SD:
      strncpy(out.type_name, "SD", sizeof(out.type_name) - 1);
      break;
    case CARD_SDHC:
      strncpy(out.type_name, "SDHC", sizeof(out.type_name) - 1);
      break;
    default:
      strncpy(out.type_name, "?", sizeof(out.type_name) - 1);
      break;
  }

  out.size_mb = static_cast<uint32_t>(SD_MMC.cardSize() / (1024ULL * 1024ULL));
  out.total_mb = static_cast<uint32_t>(SD_MMC.totalBytes() / (1024ULL * 1024ULL));
  out.used_mb = static_cast<uint32_t>(SD_MMC.usedBytes() / (1024ULL * 1024ULL));
  out.free_mb = (out.total_mb > out.used_mb) ? (out.total_mb - out.used_mb) : 0;

  out.dirs_ok = true;
  for (const char* dir : kDirs) {
    File f = SD_MMC.open(dir);
    const bool ok = f && f.isDirectory();
    if (f) {
      f.close();
    }
    if (!ok) {
      out.dirs_ok = false;
      break;
    }
  }
}

bool TfExpose::ensureDirs() {
  for (const char* dir : kDirs) {
    if (SD_MMC.exists(dir)) {
      File f = SD_MMC.open(dir);
      const bool is_dir = f && f.isDirectory();
      if (f) {
        f.close();
      }
      if (is_dir) {
        continue;
      }
      SD_MMC.remove(dir);
    }
    if (!SD_MMC.mkdir(dir)) {
      Serial.printf("TF: mkdir %s failed\n", dir);
      return false;
    }
  }
  return true;
}

bool TfExpose::checkCard(TfCardInfo& out) {
  out = {};
  const bool keep = mounted_;
  if (!mountCard()) {
    last_error_ = "no TF card";
    probe_ok_ = false;
    probed_ = true;
    return false;
  }

  fillInfo(out);
  probe_ok_ = out.present;
  probed_ = true;
  last_error_ = out.present ? "" : "no TF card";

  Serial.printf("TF: check type=%s size=%uMB free=%uMB dirs=%s\n", out.type_name, out.size_mb,
                out.free_mb, out.dirs_ok ? "ok" : "miss");

  if (!keep && !enabled_) {
    unmountCard();
  }
  return out.present;
}

bool TfExpose::formatPocketLayout(TfCardInfo& out, FormatProgress progress) {
  out = {};
  // No Serial.flush here — with MSC active, CDC flush can wedge the USB stack.
  auto tick = [&](int p, const char* log) {
    Serial.println(log);
    if (progress) {
      progress(p, log);
    }
  };

  // If Expose is on: hide media → wait for Mac → then unmount card → wipe.
  // Do not call disable() in one shot (SD_MMC.end while Mac is still reading can hang).
  if (enabled_ || msc_started_) {
    tick(5, "TF: expose off");
    // Flag off first so MSC callbacks refuse I/O immediately.
    enabled_ = false;
    persist(false);
    g_msc.mediaPresent(false);
    delay(150);
    g_msc.end();
    msc_started_ = false;

    for (int i = 0; i < 6; ++i) {
      tick(6 + i, "TF: waiting host");
      delay(500);
    }

    tick(12, "TF: unmount");
    unmountCard();
  } else {
    stopMsc();
    unmountCard();
    delay(100);
  }

  // Always raw-format: works for exFAT / corrupt FAT (no VFS mount required).
  tick(15, "TF: wipe→FAT32");
  if (!rawFormatFat32(progress)) {
    last_error_ = "format fail";
    probe_ok_ = false;
    probed_ = true;
    return false;
  }

  tick(80, "TF: mount");
  if (!tryBegin(true, 400, false) && !tryBegin(false, 400, false) &&
      !tryBegin(false, SDMMC_FREQ_DEFAULT, false)) {
    last_error_ = "remount fail";
    probe_ok_ = false;
    probed_ = true;
    return false;
  }
  mounted_ = true;

  tick(88, "TF: folders");
  if (!ensureDirs()) {
    last_error_ = "mkdir fail";
    fillInfo(out);
    unmountCard();
    return false;
  }
  fillInfo(out);

  // Re-stamp label after mkdir (safe, no dir shifting) so Mac definitely sees it.
  tick(95, "TF: label");
  SD_MMC.end();
  mounted_ = false;
  if (!rawApplyVolumeLabel()) {
    last_error_ = "label fail";
    probe_ok_ = false;
    probed_ = true;
    return false;
  }

  probe_ok_ = true;
  probed_ = true;
  last_error_ = "";
  tick(100, "TF: format OK");
  Serial.printf("TF: format OK size=%uMB label=%s\n", out.size_mb, kVolumeLabel);
  return true;
}

bool TfExpose::mountCard() {
  if (mounted_) {
    return true;
  }

  // LilyGO-style attempts: 4-bit default, then slower / 1-bit fallbacks
  if (tryBegin(false, SDMMC_FREQ_DEFAULT) || tryBegin(false, 400) || tryBegin(true, 400) ||
      tryBegin(true, SDMMC_FREQ_DEFAULT)) {
    mounted_ = true;
    last_error_ = "";
    return true;
  }

  last_error_ = "no TF card";
  mounted_ = false;
  return false;
}

void TfExpose::unmountCard() {
  if (!mounted_) {
    return;
  }
  SD_MMC.end();
  mounted_ = false;
}

bool TfExpose::startMsc() {
  if (!mounted_) {
    last_error_ = "no TF card";
    return false;
  }

  const uint32_t blocks = sectorCount();
  if (blocks == 0) {
    last_error_ = "bad geometry";
    return false;
  }

  g_msc.vendorID("PocketOps");
  g_msc.productID("PocketOps");  // USB name; FAT volume label set on Format
  g_msc.productRevision("1.0");
  g_msc.onRead(onRead);
  g_msc.onWrite(onWrite);
  g_msc.onStartStop(onStartStop);

  if (!g_msc.begin(blocks, 512)) {
    last_error_ = "msc fail";
    return false;
  }
  g_msc.mediaPresent(true);
  msc_started_ = true;

  hid::UsbHid::instance().begin();

  last_error_ = "";
  return true;
}

void TfExpose::stopMsc() {
  g_msc.mediaPresent(false);
  delay(100);   // let in-flight MSC read/write finish
  g_msc.end();  // clear callbacks + geometry
  msc_started_ = false;
}

bool TfExpose::enable() {
  if (enabled_) {
    return true;
  }
  if (!mountCard()) {
    enabled_ = false;
    return false;
  }
  if (!startMsc()) {
    unmountCard();
    enabled_ = false;
    return false;
  }
  enabled_ = true;
  probe_ok_ = true;
  probed_ = true;
  persist(true);

  // Mac often keeps "no disk" after Format cleared MSC. Soft USB re-plug so it
  // rediscovers the volume — WiFi/BLE are untouched (not ESP.restart).
  if (tud_connected()) {
    Serial.println("TF: USB re-enum for host remount");
    tud_disconnect();
    delay(400);
    tud_connect();
  }
  return true;
}

void TfExpose::disable() {
  enabled_ = false;  // MSC callbacks check this and bail
  persist(false);
  stopMsc();
  unmountCard();
  if (!last_error_ || !last_error_[0]) {
    last_error_ = "";
  }
}

bool TfExpose::toggle() {
  if (enabled_) {
    disable();
    return true;
  }
  return enable();
}

}  // namespace storage
