#include "storage/BootPrefs.h"

#include <Preferences.h>
#include <cstring>
#include <strings.h>

#include "storage/ConfigStore.h"
#include "storage/DeviceLog.h"

namespace storage {

BootPrefs& BootPrefs::instance() {
  static BootPrefs s;
  return s;
}

void BootPrefs::normalize(const char* in, char* out, size_t out_len) const {
  if (!out || out_len < 2) {
    return;
  }
  out[0] = '\0';
  if (!in || !in[0]) {
    return;
  }
  // Treat menu/off/none/empty as "stay on menu"
  if (strcasecmp(in, "menu") == 0 || strcasecmp(in, "off") == 0 || strcasecmp(in, "none") == 0 ||
      strcmp(in, "-") == 0) {
    return;
  }
  size_t n = 0;
  for (size_t i = 0; in[i] && n + 1 < out_len; ++i) {
    const char c = in[i];
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                    c == '_' || c == '-';
    if (!ok) {
      continue;
    }
    out[n++] = static_cast<char>((c >= 'A' && c <= 'Z') ? (c - 'A' + 'a') : c);
  }
  out[n] = '\0';
}

void BootPrefs::persist() {
  Preferences prefs;
  ConfigStore::instance().loadFormatFolders();
  if (!prefs.begin(ConfigStore::instance().nvsNamespace(), false)) {
    return;
  }
  prefs.putString("boot_tool", tool_id_);
  prefs.end();
}

void BootPrefs::begin() {
  tool_id_[0] = '\0';
  Preferences prefs;
  ConfigStore::instance().loadFormatFolders();
  if (prefs.begin(ConfigStore::instance().nvsNamespace(), true)) {
    String s = prefs.getString("boot_tool", "");
    prefs.end();
    normalize(s.c_str(), tool_id_, sizeof(tool_id_));
  }
  loaded_ = true;
  if (tool_id_[0]) {
    PO_LOGI("boot", "tool on boot=%s", tool_id_);
  } else {
    PO_LOGI("boot", "tool on boot=menu");
  }
}

bool BootPrefs::setToolId(const char* id) {
  char norm[kMaxIdLen];
  normalize(id, norm, sizeof(norm));
  if (strcmp(norm, tool_id_) == 0 && loaded_) {
    return true;
  }
  strncpy(tool_id_, norm, sizeof(tool_id_) - 1);
  tool_id_[sizeof(tool_id_) - 1] = '\0';
  persist();
  loaded_ = true;
  PO_LOGI("boot", "tool on boot set=%s", tool_id_[0] ? tool_id_ : "menu");
  return true;
}

}  // namespace storage
