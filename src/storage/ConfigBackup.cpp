#include "storage/ConfigBackup.h"

#include <FS.h>
#include <LittleFS.h>
#include <cstdlib>
#include <cstring>
#include <strings.h>

#include "storage/BootPrefs.h"
#include "storage/ConfigStore.h"
#include "storage/DeviceLog.h"
#include "storage/FirmwareUpdate.h"
#include "storage/LanProfiles.h"
#include "storage/TfExpose.h"
#include "storage/UsbConsole.h"
#include "storage/WifiProfiles.h"

namespace storage {
namespace {

struct BackupFile {
  const char* name;
  const char* path;
};

constexpr BackupFile kFiles[] = {
    {"pocketops.ini", "/config/pocketops.ini"},
    {"pocketops_bt.ini", "/config/pocketops_bt.ini"},
    {"pocketops_mouse.ini", "/config/pocketops_mouse.ini"},
    {"pocketops_keyboard.ini", "/config/pocketops_keyboard.ini"},
    {"pocketops_folders.ini", "/config/pocketops_folders.ini"},
    {"pocketops_ui.ini", "/config/pocketops_ui.ini"},
    {"pocketops_display.ini", "/config/pocketops_display.ini"},
    {"wifi_profiles.ini", "/config/wifi_profiles.ini"},
};

constexpr const char* kPrefsName = "pocketops_prefs.ini";
constexpr size_t kFileCount = sizeof(kFiles) / sizeof(kFiles[0]);
constexpr size_t kIoChunk = 512;

/** Running CRC (init with 0xFFFFFFFF; finalize with ~crc). */
uint32_t crc32Run(uint32_t crc, const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int b = 0; b < 8; ++b) {
      const uint32_t mask = -(crc & 1u);
      crc = (crc >> 1) ^ (0xEDB88320u & mask);
    }
  }
  return crc;
}

uint32_t crc32Update(uint32_t /*seed*/, const uint8_t* data, size_t len) {
  return ~crc32Run(0xFFFFFFFFu, data, len);
}

bool writeBytes(File& f, const uint8_t* data, size_t len) {
  if (!len) {
    return true;
  }
  return f.write(data, len) == len;
}

bool writeU16(File& f, uint16_t v) {
  const uint8_t b[2] = {static_cast<uint8_t>(v & 0xFF), static_cast<uint8_t>((v >> 8) & 0xFF)};
  return writeBytes(f, b, 2);
}

bool writeU32(File& f, uint32_t v) {
  const uint8_t b[4] = {static_cast<uint8_t>(v & 0xFF), static_cast<uint8_t>((v >> 8) & 0xFF),
                        static_cast<uint8_t>((v >> 16) & 0xFF), static_cast<uint8_t>((v >> 24) & 0xFF)};
  return writeBytes(f, b, 4);
}

bool readExact(File& f, uint8_t* dst, size_t n) {
  size_t got = 0;
  while (got < n) {
    const size_t r = f.read(dst + got, n - got);
    if (r == 0) {
      return false;
    }
    got += r;
  }
  return true;
}

uint32_t readU32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

uint16_t readU16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

void setErr(char* err_out, size_t err_len, const char* msg) {
  if (!err_out || err_len == 0) {
    return;
  }
  strncpy(err_out, msg ? msg : "error", err_len - 1);
  err_out[err_len - 1] = '\0';
}

uint32_t crcOfFile(File& f) {
  const size_t mark = f.position();
  f.seek(0);
  uint32_t crc = 0xFFFFFFFFu;
  uint8_t buf[kIoChunk];
  while (true) {
    const size_t n = f.read(buf, sizeof(buf));
    if (!n) {
      break;
    }
    crc = crc32Run(crc, buf, n);
  }
  f.seek(mark);
  return ~crc;
}

bool copyFileChunked(File& in, File& out, size_t nbytes) {
  uint8_t buf[kIoChunk];
  size_t left = nbytes;
  while (left) {
    const size_t want = left > sizeof(buf) ? sizeof(buf) : left;
    if (!readExact(in, buf, want)) {
      return false;
    }
    if (!writeBytes(out, buf, want)) {
      return false;
    }
    left -= want;
  }
  return true;
}

bool appendLocalEntry(File& out, const char* name, File& body, uint32_t body_len, uint32_t crc,
                      uint32_t& local_offset_out) {
  local_offset_out = static_cast<uint32_t>(out.position());
  const uint16_t name_len = static_cast<uint16_t>(strlen(name));
  if (!writeU32(out, 0x04034b50u) || !writeU16(out, 20) || !writeU16(out, 0) || !writeU16(out, 0) ||
      !writeU16(out, 0) || !writeU16(out, 0) || !writeU32(out, crc) || !writeU32(out, body_len) ||
      !writeU32(out, body_len) || !writeU16(out, name_len) || !writeU16(out, 0) ||
      !writeBytes(out, reinterpret_cast<const uint8_t*>(name), name_len)) {
    return false;
  }
  if (body_len) {
    body.seek(0);
    if (!copyFileChunked(body, out, body_len)) {
      return false;
    }
  }
  return true;
}

bool appendLocalEntryMem(File& out, const char* name, const uint8_t* body, uint32_t body_len,
                         uint32_t& local_offset_out, uint32_t& crc_out) {
  local_offset_out = static_cast<uint32_t>(out.position());
  crc_out = crc32Update(0, body, body_len);
  const uint16_t name_len = static_cast<uint16_t>(strlen(name));
  if (!writeU32(out, 0x04034b50u) || !writeU16(out, 20) || !writeU16(out, 0) || !writeU16(out, 0) ||
      !writeU16(out, 0) || !writeU16(out, 0) || !writeU32(out, crc_out) || !writeU32(out, body_len) ||
      !writeU32(out, body_len) || !writeU16(out, name_len) || !writeU16(out, 0) ||
      !writeBytes(out, reinterpret_cast<const uint8_t*>(name), name_len)) {
    return false;
  }
  if (body_len && !writeBytes(out, body, body_len)) {
    return false;
  }
  return true;
}

void buildPrefsIni(uint8_t* buf, size_t buflen, size_t* out_len) {
  const char* boot = BootPrefs::instance().toolId();
  snprintf(reinterpret_cast<char*>(buf), buflen,
           "; PocketOps NVS preferences (restored with .pobak)\n"
           "; expose_boot / boot_tool / usb_serial / device_log / usb_dfu\n"
           "expose_boot=%d\n"
           "boot_tool=%s\n"
           "usb_serial=%d\n"
           "device_log=%d\n"
           "usb_dfu=%d\n",
           TfExpose::instance().bootEnabled() ? 1 : 0, (boot && boot[0]) ? boot : "menu",
           UsbConsole::instance().enabled() ? 1 : 0, DeviceLog::instance().enabled() ? 1 : 0,
           FirmwareUpdate::instance().dfuEnabled() ? 1 : 0);
  *out_len = strlen(reinterpret_cast<char*>(buf));
}

bool parseBool01(const char* v) {
  if (!v || !v[0]) {
    return false;
  }
  return v[0] == '1' || strcasecmp(v, "true") == 0 || strcasecmp(v, "on") == 0;
}

bool applyPrefsIni(const uint8_t* data, size_t len) {
  if (!data) {
    return false;
  }
  char* buf = static_cast<char*>(malloc(len + 1));
  if (!buf) {
    return false;
  }
  memcpy(buf, data, len);
  buf[len] = '\0';

  char* save = nullptr;
  for (char* line = strtok_r(buf, "\n", &save); line; line = strtok_r(nullptr, "\n", &save)) {
    while (*line == ' ' || *line == '\t' || *line == '\r') {
      ++line;
    }
    if (!*line || *line == ';' || *line == '#') {
      continue;
    }
    char* eq = strchr(line, '=');
    if (!eq) {
      continue;
    }
    *eq = '\0';
    char* key = line;
    char* val = eq + 1;
    char* ke = key + strlen(key);
    while (ke > key && (ke[-1] == ' ' || ke[-1] == '\t' || ke[-1] == '\r')) {
      *--ke = '\0';
    }
    while (*val == ' ' || *val == '\t' || *val == '\r') {
      ++val;
    }
    char* ve = val + strlen(val);
    while (ve > val && (ve[-1] == ' ' || ve[-1] == '\t' || ve[-1] == '\r')) {
      *--ve = '\0';
    }

    if (strcmp(key, "expose_boot") == 0) {
      TfExpose::instance().setBootEnabled(parseBool01(val));
    } else if (strcmp(key, "boot_tool") == 0) {
      BootPrefs::instance().setToolId(val);
    } else if (strcmp(key, "usb_serial") == 0) {
      UsbConsole::instance().setEnabled(parseBool01(val));
    } else if (strcmp(key, "device_log") == 0) {
      DeviceLog::instance().setEnabled(parseBool01(val));
    } else if (strcmp(key, "usb_dfu") == 0) {
      FirmwareUpdate::instance().setDfuEnabled(parseBool01(val));
    }
  }
  free(buf);
  return true;
}

bool isPrefsName(const char* name) {
  return name && strcmp(name, kPrefsName) == 0;
}

const BackupFile* findAllowlisted(const char* name) {
  if (!name || !name[0]) {
    return nullptr;
  }
  if (strchr(name, '/') || strchr(name, '\\') || strstr(name, "..")) {
    return nullptr;
  }
  if (isPrefsName(name)) {
    return nullptr;
  }
  for (size_t i = 0; i < kFileCount; ++i) {
    if (strcmp(name, kFiles[i].name) == 0) {
      return &kFiles[i];
    }
  }
  return nullptr;
}

bool writeFileBytes(const char* path, const uint8_t* data, size_t len) {
  File f = LittleFS.open(path, "w");
  if (!f) {
    return false;
  }
  if (len > 0) {
    const size_t w = f.write(data, len);
    f.close();
    return w == len;
  }
  f.close();
  return true;
}

bool ensureConfigDir() {
  return LittleFS.exists("/config") || LittleFS.mkdir("/config");
}

}  // namespace

void ConfigBackup::removeTemp(const char* path) {
  if (path && LittleFS.exists(path)) {
    LittleFS.remove(path);
  }
}

bool ConfigBackup::buildZipToFile(const char* path) {
  if (!path || !path[0] || !ensureConfigDir()) {
    return false;
  }
  removeTemp(path);

  File out = LittleFS.open(path, "w");
  if (!out) {
    return false;
  }

  struct EntryMeta {
    const char* name;
    uint32_t crc;
    uint32_t size;
    uint32_t local_offset;
  };
  EntryMeta metas[kFileCount + 1];
  size_t meta_n = 0;
  size_t total = 0;

  for (size_t i = 0; i < kFileCount; ++i) {
    File body = LittleFS.open(kFiles[i].path, "r");
    if (!body) {
      continue;
    }
    const size_t n = body.size();
    if (n > kMaxEntryBytes || total + n + 128 > kMaxZipBytes) {
      body.close();
      out.close();
      removeTemp(path);
      return false;
    }
    const uint32_t crc = crcOfFile(body);
    EntryMeta& m = metas[meta_n];
    m.name = kFiles[i].name;
    m.size = static_cast<uint32_t>(n);
    m.crc = crc;
    if (!appendLocalEntry(out, m.name, body, m.size, m.crc, m.local_offset)) {
      body.close();
      out.close();
      removeTemp(path);
      return false;
    }
    body.close();
    total += n + 64;
    ++meta_n;
  }

  {
    uint8_t prefs[320];
    size_t prefs_len = 0;
    buildPrefsIni(prefs, sizeof(prefs), &prefs_len);
    EntryMeta& m = metas[meta_n];
    m.name = kPrefsName;
    m.size = static_cast<uint32_t>(prefs_len);
    if (!appendLocalEntryMem(out, m.name, prefs, m.size, m.local_offset, m.crc)) {
      out.close();
      removeTemp(path);
      return false;
    }
    ++meta_n;
  }

  if (meta_n == 0) {
    out.close();
    removeTemp(path);
    return false;
  }

  const uint32_t cd_offset = static_cast<uint32_t>(out.position());
  for (size_t i = 0; i < meta_n; ++i) {
    const EntryMeta& m = metas[i];
    const uint16_t name_len = static_cast<uint16_t>(strlen(m.name));
    if (!writeU32(out, 0x02014b50u) || !writeU16(out, 20) || !writeU16(out, 20) || !writeU16(out, 0) ||
        !writeU16(out, 0) || !writeU16(out, 0) || !writeU16(out, 0) || !writeU32(out, m.crc) ||
        !writeU32(out, m.size) || !writeU32(out, m.size) || !writeU16(out, name_len) ||
        !writeU16(out, 0) || !writeU16(out, 0) || !writeU16(out, 0) || !writeU16(out, 0) ||
        !writeU32(out, 0) || !writeU32(out, m.local_offset) ||
        !writeBytes(out, reinterpret_cast<const uint8_t*>(m.name), name_len)) {
      out.close();
      removeTemp(path);
      return false;
    }
  }
  const uint32_t cd_size = static_cast<uint32_t>(out.position()) - cd_offset;

  if (!writeU32(out, 0x06054b50u) || !writeU16(out, 0) || !writeU16(out, 0) ||
      !writeU16(out, static_cast<uint16_t>(meta_n)) || !writeU16(out, static_cast<uint16_t>(meta_n)) ||
      !writeU32(out, cd_size) || !writeU32(out, cd_offset) || !writeU16(out, 0)) {
    out.close();
    removeTemp(path);
    return false;
  }

  out.close();
  return true;
}

bool ConfigBackup::applyZipFromFile(const char* path, char* err_out, size_t err_len) {
  if (!path || !path[0]) {
    setErr(err_out, err_len, "Invalid or too large zip");
    return false;
  }
  File f = LittleFS.open(path, "r");
  if (!f) {
    setErr(err_out, err_len, "Invalid or too large zip");
    return false;
  }
  const size_t len = f.size();
  if (len < 22 || len > kMaxZipBytes) {
    f.close();
    setErr(err_out, err_len, "Invalid or too large zip");
    return false;
  }

  size_t restored = 0;
  while (f.position() + 30 <= len) {
    uint8_t hdr[30];
    if (!readExact(f, hdr, 30)) {
      break;
    }
    if (readU32(hdr) != 0x04034b50u) {
      break;
    }
    const uint16_t method = readU16(hdr + 8);
    const uint32_t crc_exp = readU32(hdr + 14);
    const uint32_t comp_size = readU32(hdr + 18);
    const uint32_t uncomp_size = readU32(hdr + 22);
    const uint16_t name_len = readU16(hdr + 26);
    const uint16_t extra_len = readU16(hdr + 28);

    if (method != 0) {
      f.close();
      setErr(err_out, err_len, "Only uncompressed (store) zip supported");
      return false;
    }
    if (comp_size != uncomp_size || uncomp_size > kMaxEntryBytes) {
      f.close();
      setErr(err_out, err_len, "Corrupt zip sizes");
      return false;
    }
    if (name_len == 0 || name_len >= 96) {
      f.close();
      setErr(err_out, err_len, "Bad entry name");
      return false;
    }

    char name[96];
    if (!readExact(f, reinterpret_cast<uint8_t*>(name), name_len)) {
      f.close();
      setErr(err_out, err_len, "Truncated zip entry");
      return false;
    }
    name[name_len] = '\0';
    if (extra_len) {
      if (f.position() + extra_len > len) {
        f.close();
        setErr(err_out, err_len, "Truncated zip entry");
        return false;
      }
      f.seek(f.position() + extra_len);
    }

    uint8_t* payload = nullptr;
    if (uncomp_size) {
      payload = static_cast<uint8_t*>(malloc(uncomp_size));
      if (!payload) {
        f.close();
        setErr(err_out, err_len, "Out of memory");
        return false;
      }
      if (!readExact(f, payload, uncomp_size)) {
        free(payload);
        f.close();
        setErr(err_out, err_len, "Truncated zip entry");
        return false;
      }
    }

    const uint32_t crc = crc32Update(0, payload, uncomp_size);
    if (crc != crc_exp) {
      free(payload);
      f.close();
      setErr(err_out, err_len, "CRC mismatch");
      return false;
    }

    const char* base = name;
    if (strncmp(name, "config/", 7) == 0) {
      base = name + 7;
    }

    bool ok_entry = true;
    if (isPrefsName(base)) {
      ok_entry = applyPrefsIni(payload, uncomp_size);
      if (ok_entry) {
        ++restored;
      } else {
        free(payload);
        f.close();
        setErr(err_out, err_len, "Prefs restore failed");
        return false;
      }
    } else {
      const BackupFile* slot = findAllowlisted(base);
      if (slot) {
        if (!writeFileBytes(slot->path, payload, uncomp_size)) {
          free(payload);
          f.close();
          setErr(err_out, err_len, "Write failed");
          return false;
        }
        ++restored;
      }
    }
    free(payload);
    (void)ok_entry;
  }

  f.close();
  if (restored == 0) {
    setErr(err_out, err_len, "No known config files in zip");
    return false;
  }
  setErr(err_out, err_len, "OK");
  return true;
}

bool ConfigBackup::factoryReset(const FactoryResetOpts& opts, char* err_out, size_t err_len) {
  if (!opts.settings && !opts.nvs_prefs && !opts.wifi_profiles && !opts.lan_profiles &&
      !opts.device_logs) {
    setErr(err_out, err_len, "Select at least one reset option");
    return false;
  }

  if (opts.settings) {
    if (!ConfigStore::instance().restoreFactoryDefaults()) {
      setErr(err_out, err_len, "Settings restore failed");
      return false;
    }
  }

  if (opts.nvs_prefs) {
    TfExpose::instance().setBootEnabled(false);
    BootPrefs::instance().setToolId("menu");
    UsbConsole::instance().setEnabled(false);
    DeviceLog::instance().setEnabled(false);
    FirmwareUpdate::instance().setDfuEnabled(false);
  }

  if (opts.wifi_profiles) {
    if (!WifiProfiles::instance().clearAll()) {
      setErr(err_out, err_len, "Wi-Fi profiles clear failed");
      return false;
    }
  }

  if (opts.lan_profiles) {
    if (!LanProfiles::instance().clearAll()) {
      setErr(err_out, err_len, "LAN profiles clear failed");
      return false;
    }
  }

  if (opts.device_logs) {
    DeviceLog::instance().clearAll();
  }

  setErr(err_out, err_len, "OK");
  return true;
}

}  // namespace storage
