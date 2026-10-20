#include "storage/TfExpose.h"

#include <Preferences.h>
#include <SD_MMC.h>
#include <string.h>

#include "USBMSC.h"
#include "display/Display.h"
#include "hid/UsbHid.h"
#include "Branding.h"
#include "board_pins.h"
#include "storage/ConfigStore.h"
#include "storage/DeviceLog.h"
#include "storage/TfExposeInternal.h"
#include "storage/UsbConsole.h"
#include "tusb.h"


namespace storage {
using namespace tf_internal;

namespace {

bool removeTree(const char* path);

}  // namespace

bool TfExpose::writeTextFile(const char* path, const char* data) {
  if (!path || path[0] != '/' || !data) {
    last_error_ = "bad path";
    return false;
  }
  if (!beginExclusive()) {
    return false;
  }
  char parent[kPathBuf];
  strncpy(parent, path, sizeof(parent) - 1);
  parent[sizeof(parent) - 1] = '\0';
  char* slash = strrchr(parent, '/');
  if (slash && slash != parent) {
    *slash = '\0';
    if (!ensureDirTree(parent)) {
      last_error_ = "mkdir fail";
      endExclusive();
      return false;
    }
  }
  File f = SD_MMC.open(path, FILE_WRITE);
  if (!f) {
    last_error_ = "open fail";
    endExclusive();
    return false;
  }
  f.print(data);
  f.close();
  noteActivity();
  last_error_ = "";
  endExclusive();
  return true;
}

bool TfExpose::sanitizePath(const char* path, char* out, size_t out_len) {
  if (!path || !out || out_len < 2 || path[0] != '/') {
    return false;
  }
  if (strstr(path, "..")) {
    return false;
  }
  size_t n = 0;
  out[n++] = '/';
  for (size_t i = 1; path[i] && n + 1 < out_len; ++i) {
    const char c = path[i];
    if (c == '/' && out[n - 1] == '/') {
      continue;  // collapse //
    }
    out[n++] = c;
  }
  out[n] = '\0';
  // trim trailing slash except root
  while (n > 1 && out[n - 1] == '/') {
    out[--n] = '\0';
  }
  return true;
}

bool TfExpose::beginExclusive() {
  if (exclusive_) {
    return mounted_;
  }

  // Never steal the card from the Mac mid-Expose — that hangs the unit and can
  // leave the host seeing an empty volume. Browser FS requires Expose off.
  if (enabled_ || msc_started_) {
    last_error_ = "expose_on";
    exclusive_restore_expose_ = false;
    return false;
  }

  // After Expose off, wait so we do not remount under a ghost Mac volume.
  waitMscCooloff();

  exclusive_restore_expose_ = false;
  if (!mounted_) {
    if (!mountCard()) {
      // One retry after a short settle (bus still recovering from MSC).
      delay(200);
      yield();
      if (!mountCard()) {
        last_error_ = "no TF card";
        exclusive_ = false;
        PO_LOGW("sd", "exclusive mount failed");
        return false;
      }
    }
  }
  // Recreate PocketOps folders if missing (e.g. /Logs) without formatting.
  ensureDirs();
  exclusive_ = true;
  probe_ok_ = true;
  probed_ = true;
  last_error_ = "";
  noteActivity();
  return true;
}

void TfExpose::endExclusive() {
  if (!exclusive_) {
    return;
  }
  exclusive_ = false;
  // Keep the card mounted while Expose is off so the next list/upload is fast
  // and we do not thrash SD_MMC.begin/end (that also confuses some hosts).
  last_error_ = "";
}

bool TfExpose::pathExists(const char* path) const {
  char clean[kPathBuf];
  if (!sanitizePath(path, clean, sizeof(clean))) {
    return false;
  }
  return mounted_ && SD_MMC.exists(clean);
}

bool TfExpose::isDirectory(const char* path) const {
  char clean[kPathBuf];
  if (!sanitizePath(path, clean, sizeof(clean))) {
    return false;
  }
  if (!mounted_ || !SD_MMC.exists(clean)) {
    return false;
  }
  File f = SD_MMC.open(clean);
  const bool dir = f && f.isDirectory();
  if (f) {
    f.close();
  }
  return dir;
}

File TfExpose::openFile(const char* path, const char* mode) {
  char clean[kPathBuf];
  if (!sanitizePath(path, clean, sizeof(clean)) || !mounted_) {
    return File();
  }
  if (mode && (mode[0] == 'w' || mode[0] == 'a')) {
    char parent[kPathBuf];
    strncpy(parent, clean, sizeof(parent) - 1);
    parent[sizeof(parent) - 1] = '\0';
    char* slash = strrchr(parent, '/');
    if (slash && slash != parent) {
      *slash = '\0';
      ensureDirTree(parent);
    }
  }
  File f = SD_MMC.open(clean, mode);
  if (f) {
    noteActivity();
  }
  return f;
}

bool TfExpose::listDirectory(const char* path, String& json_array_out) {
  char clean[kPathBuf];
  if (!sanitizePath(path && path[0] ? path : "/", clean, sizeof(clean))) {
    last_error_ = "bad path";
    return false;
  }
  if (!mounted_) {
    last_error_ = "not mounted";
    return false;
  }
  File dir = SD_MMC.open(clean);
  if (!dir || !dir.isDirectory()) {
    if (dir) {
      dir.close();
    }
    last_error_ = "not a directory";
    return false;
  }
  json_array_out = '[';
  bool first = true;
  size_t n_entries = 0;
  constexpr size_t kMaxListEntries = 200;
  File entry = dir.openNextFile();
  while (entry) {
    if (n_entries >= kMaxListEntries) {
      entry.close();
      break;
    }
    if (!first) {
      json_array_out += ',';
    }
    first = false;
    ++n_entries;
    const char* name = entry.name();
    // SD_MMC may return full path — take basename
    const char* base = name;
    if (const char* slash = strrchr(name, '/')) {
      base = slash + 1;
    }
    char name_esc[96];
    size_t o = 0;
    for (size_t i = 0; base[i] && o + 2 < sizeof(name_esc); ++i) {
      const char c = base[i];
      if (c == '"' || c == '\\') {
        name_esc[o++] = '\\';
      }
      if (static_cast<uint8_t>(c) < 0x20) {
        continue;
      }
      name_esc[o++] = c;
    }
    name_esc[o] = '\0';
    char full[kPathBuf];
    if (strcmp(clean, "/") == 0) {
      snprintf(full, sizeof(full), "/%s", base);
    } else {
      snprintf(full, sizeof(full), "%s/%s", clean, base);
    }
    char full_esc[kPathBuf * 2];
    o = 0;
    for (size_t i = 0; full[i] && o + 2 < sizeof(full_esc); ++i) {
      const char c = full[i];
      if (c == '"' || c == '\\') {
        full_esc[o++] = '\\';
      }
      full_esc[o++] = c;
    }
    full_esc[o] = '\0';
    json_array_out += "{\"name\":\"";
    json_array_out += name_esc;
    json_array_out += "\",\"path\":\"";
    json_array_out += full_esc;
    json_array_out += "\",\"dir\":";
    json_array_out += entry.isDirectory() ? "true" : "false";
    json_array_out += ",\"size\":";
    json_array_out += String(static_cast<unsigned long>(entry.size()));
    json_array_out += '}';
    entry.close();
    if ((n_entries & 7u) == 0) {
      yield();
    }
    entry = dir.openNextFile();
  }
  dir.close();
  json_array_out += ']';
  noteActivity();
  last_error_ = "";
  return true;
}

bool TfExpose::makeDir(const char* path) {
  char clean[kPathBuf];
  if (!sanitizePath(path, clean, sizeof(clean))) {
    last_error_ = "bad path";
    return false;
  }
  if (!mounted_) {
    last_error_ = "not mounted";
    return false;
  }
  if (!ensureDirTree(clean)) {
    last_error_ = "mkdir fail";
    return false;
  }
  noteActivity();
  last_error_ = "";
  return true;
}

namespace {

bool removeTree(const char* path) {
  File dir = SD_MMC.open(path);
  if (!dir) {
    return false;
  }
  if (!dir.isDirectory()) {
    dir.close();
    return SD_MMC.remove(path);
  }
  File entry = dir.openNextFile();
  while (entry) {
    char child[96];
    const char* name = entry.name();
    const char* base = name;
    if (const char* slash = strrchr(name, '/')) {
      base = slash + 1;
    }
    if (strcmp(path, "/") == 0) {
      snprintf(child, sizeof(child), "/%s", base);
    } else {
      snprintf(child, sizeof(child), "%s/%s", path, base);
    }
    const bool is_dir = entry.isDirectory();
    entry.close();
    if (is_dir) {
      if (!removeTree(child)) {
        dir.close();
        return false;
      }
    } else if (!SD_MMC.remove(child)) {
      dir.close();
      return false;
    }
    entry = dir.openNextFile();
  }
  dir.close();
  if (strcmp(path, "/") == 0) {
    return true;
  }
  return SD_MMC.rmdir(path);
}

}  // namespace

bool TfExpose::removePath(const char* path) {
  char clean[kPathBuf];
  if (!sanitizePath(path, clean, sizeof(clean))) {
    last_error_ = "bad path";
    return false;
  }
  if (strcmp(clean, "/") == 0) {
    last_error_ = "refuse root";
    return false;
  }
  if (!mounted_ || !SD_MMC.exists(clean)) {
    last_error_ = "missing";
    return false;
  }
  if (!removeTree(clean)) {
    last_error_ = "delete fail";
    return false;
  }
  noteActivity();
  last_error_ = "";
  return true;
}



}  // namespace storage
