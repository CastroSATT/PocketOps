#include "tools/remote_hid/SdStatusApi.h"

#include <FS.h>
#include <LittleFS.h>
#include <SD_MMC.h>
#include <cstring>

#include "Branding.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "storage/ConfigStore.h"
#include "storage/DeviceLog.h"
#include "storage/TfExpose.h"
#include "storage/UsbConsole.h"
#include "tools/remote_hid/HttpCtx.h"
#include "util/JsonEsc.h"

namespace remote_hid {
namespace {

using util::jsonEscape;

void appendStorageJson(char* body, size_t body_len, bool sd_detail) {
  // LittleFS is always mounted after ConfigStore::begin — cheap used/total.
  const uint32_t lfs_total = static_cast<uint32_t>(LittleFS.totalBytes());
  const uint32_t lfs_used = static_cast<uint32_t>(LittleFS.usedBytes());

  auto& tf = storage::TfExpose::instance();
  uint32_t sd_total_mb = 0;
  uint32_t sd_used_mb = 0;
  bool sd_space = false;
  if (tf.mounted() || tf.exclusiveHeld()) {
    const uint64_t tot = SD_MMC.totalBytes();
    const uint64_t used = SD_MMC.usedBytes();
    if (tot > 0) {
      sd_total_mb = static_cast<uint32_t>(tot / (1024ULL * 1024ULL));
      sd_used_mb = static_cast<uint32_t>(used / (1024ULL * 1024ULL));
      sd_space = true;
    }
  } else if (sd_detail) {
    storage::TfCardInfo info;
    if (tf.checkCard(info) && info.total_mb > 0) {
      sd_total_mb = info.total_mb;
      sd_used_mb = info.used_mb;
      sd_space = true;
    }
  }

  snprintf(body, body_len,
           "\"lfs_used\":%u,\"lfs_total\":%u,\"sd_used_mb\":%u,\"sd_total_mb\":%u,\"sd_space\":%s",
           static_cast<unsigned>(lfs_used), static_cast<unsigned>(lfs_total),
           static_cast<unsigned>(sd_used_mb), static_cast<unsigned>(sd_total_mb),
           sd_space ? "true" : "false");
}

bool formatSdIconJsonImpl(char* body, size_t body_len) {
  if (!body || body_len < 64) {
    return false;
  }
  // Lightweight — powers header badge. Periodically re-probes card presence for Files tab.
  auto& tf = storage::TfExpose::instance();
  static uint32_t last_probe_ms = 0;
  if (millis() - last_probe_ms > 4000u) {
    last_probe_ms = millis();
    if (!tf.enabled() && !tf.mounted()) {
      tf.refreshCardPresent();
    }
  }
  const bool expose = tf.enabled();
  const bool files_ui = tf.filesUiOpen() || tf.exclusiveHeld();
  // MSC host activity, or Files browser holding / touching the card.
  const bool activity = tf.activityRecent() || tf.exclusiveHeld();
  const bool present = tf.cardPresent() || tf.mounted() || tf.enabled() || tf.exclusiveHeld();
  // Files tab: card installed only (USB Expose not required).
  const bool files_tab = present;

  char storage[160];
  appendStorageJson(storage, sizeof(storage), false);

  const int n =
      snprintf(body, body_len,
               "{\"expose\":%s,\"activity\":%s,\"present\":%s,\"files_tab\":%s,\"files_ui\":%s,%s}",
               expose ? "true" : "false", activity ? "true" : "false", present ? "true" : "false",
               files_tab ? "true" : "false", files_ui ? "true" : "false", storage);
  return n > 0 && static_cast<size_t>(n) < body_len;
}

void handleSdStatusGet() {
  auto& tf = storage::TfExpose::instance();
  auto& cfg = storage::ConfigStore::instance();
  cfg.loadFormatFolders();

  storage::TfCardInfo info;
  const bool present = tf.checkCard(info);
  const char* err = tf.lastError();

  char type_esc[16];
  char label_esc[24];
  char rev_esc[16];
  char err_esc[48];
  jsonEscape(info.type_name, type_esc, sizeof(type_esc));
  jsonEscape(cfg.volumeLabel(), label_esc, sizeof(label_esc));
  jsonEscape(branding::kFirmwareVersion, rev_esc, sizeof(rev_esc));
  jsonEscape(err && err[0] ? err : "", err_esc, sizeof(err_esc));

  char storage[160];
  appendStorageJson(storage, sizeof(storage), false);
  // Prefer freshly probed SD numbers from checkCard when present.
  if (present && info.total_mb > 0) {
    snprintf(storage, sizeof(storage),
             "\"lfs_used\":%u,\"lfs_total\":%u,\"sd_used_mb\":%u,\"sd_total_mb\":%u,\"sd_space\":true",
             static_cast<unsigned>(LittleFS.usedBytes()),
             static_cast<unsigned>(LittleFS.totalBytes()),
             static_cast<unsigned>(info.used_mb), static_cast<unsigned>(info.total_mb));
  }

  char body[720];
  snprintf(body, sizeof(body),
           "{\"expose\":%s,\"expose_boot\":%s,\"mounted\":%s,\"present\":%s,\"type\":\"%s\","
           "\"size_mb\":%u,\"total_mb\":%u,\"used_mb\":%u,\"free_mb\":%u,\"dirs_ok\":%s,"
           "\"volume_label\":\"%s\",\"usb_vendor\":\"PocketOps\",\"usb_product\":\"%s\","
           "\"usb_revision\":\"%s\",\"error\":\"%s\",\"activity\":%s,%s}",
           tf.enabled() ? "true" : "false", tf.bootEnabled() ? "true" : "false",
           tf.mounted() ? "true" : "false", present ? "true" : "false", type_esc,
           static_cast<unsigned>(info.size_mb), static_cast<unsigned>(info.total_mb),
           static_cast<unsigned>(info.used_mb), static_cast<unsigned>(info.free_mb),
           info.dirs_ok ? "true" : "false", label_esc, label_esc, rev_esc, err_esc,
           (tf.enabled() && tf.activityRecent()) ? "true" : "false", storage);
  g_http->send(200, "application/json", body);
}

void handleSdExposeBootPost() {
  if (!g_http->hasArg("plain")) {
    g_http->send(400, "text/plain", "Expected body: on or off");
    return;
  }
  String body = g_http->arg("plain");
  body.trim();
  body.toLowerCase();
  auto& tf = storage::TfExpose::instance();
  if (body == "on" || body == "1" || body == "true") {
    tf.setBootEnabled(true);
  } else if (body == "off" || body == "0" || body == "false") {
    tf.setBootEnabled(false);
  } else {
    g_http->send(400, "text/plain", "Expected body: on or off");
    return;
  }
  handleSdStatusGet();
}

void handleSdExposePost() {
  if (!g_http->hasArg("plain")) {
    g_http->send(400, "text/plain", "Expected body: on or off");
    return;
  }
  String body = g_http->arg("plain");
  body.trim();
  body.toLowerCase();

  auto& tf = storage::TfExpose::instance();
  if (body == "on" || body == "1" || body == "true") {
    PoSer().println("WWW: Expose on");
    if (!tf.enable()) {
      const char* err = tf.lastError();
      g_http->send(500, "text/plain", err && err[0] ? err : "expose enable failed");
      return;
    }
  } else if (body == "off" || body == "0" || body == "false") {
    PoSer().println("WWW: Expose off");
    tf.disable();
  } else if (body == "safe-off") {
    PoSer().println("WWW: Expose safe-off");
    if (!tf.disableSafe()) {
      const char* err = tf.lastError();
      if (err && strcmp(err, "sd_busy") == 0) {
        g_http->send(409, "application/json",
                     "{\"error\":\"sd_busy\",\"message\":\"SD is busy — eject on the Mac or wait, "
                     "then try again.\"}");
        return;
      }
      g_http->send(500, "text/plain", err && err[0] ? err : "safe-off failed");
      return;
    }
  } else {
    g_http->send(400, "text/plain", "Expected body: on, off, or safe-off");
    return;
  }
  handleSdStatusGet();
}

struct SdFormatJob {
  volatile bool busy = false;
  volatile bool done = false;
  volatile bool ok = false;
  volatile int percent = 0;
  char status[48] = {};
  char message[96] = {};
} g_sd_fmt;

void setSdFormatProgress(int percent, const char* status) {
  if (percent < 0) {
    percent = 0;
  }
  if (percent > 100) {
    percent = 100;
  }
  g_sd_fmt.percent = percent;
  if (!status) {
    return;
  }
  const char* line = status;
  if (strncmp(status, "TF: ", 4) == 0) {
    line = status + 4;
  }
  strncpy(g_sd_fmt.status, line, sizeof(g_sd_fmt.status) - 1);
  g_sd_fmt.status[sizeof(g_sd_fmt.status) - 1] = '\0';
}

void sdFormatTask(void* /*arg*/) {
  storage::TfCardInfo info;
  setSdFormatProgress(0, "starting");
  const bool ok = storage::TfExpose::instance().formatPocketLayout(
      info, [](int p, const char* s) { setSdFormatProgress(p, s); });
  g_sd_fmt.ok = ok;
  if (ok) {
    snprintf(g_sd_fmt.message, sizeof(g_sd_fmt.message), "Format OK %uMB label=%s",
             static_cast<unsigned>(info.size_mb),
             storage::ConfigStore::instance().volumeLabel());
    setSdFormatProgress(100, "format OK");
  } else {
    const char* err = storage::TfExpose::instance().lastError();
    strncpy(g_sd_fmt.message, err && err[0] ? err : "format fail", sizeof(g_sd_fmt.message) - 1);
    g_sd_fmt.message[sizeof(g_sd_fmt.message) - 1] = '\0';
  }
  g_sd_fmt.done = true;
  g_sd_fmt.busy = false;
  vTaskDelete(nullptr);
}

void handleSdFormatProgressGet() {
  char status_esc[96];
  char msg_esc[160];
  jsonEscape(g_sd_fmt.status, status_esc, sizeof(status_esc));
  jsonEscape(g_sd_fmt.message, msg_esc, sizeof(msg_esc));
  char body[320];
  snprintf(body, sizeof(body),
           "{\"busy\":%s,\"done\":%s,\"ok\":%s,\"percent\":%d,\"status\":\"%s\",\"message\":\"%s\"}",
           g_sd_fmt.busy ? "true" : "false", g_sd_fmt.done ? "true" : "false",
           g_sd_fmt.ok ? "true" : "false", g_sd_fmt.percent, status_esc, msg_esc);
  g_http->send(200, "application/json", body);
}

void handleSdFormatPost() {
  if (!g_http->hasArg("plain")) {
    g_http->send(400, "text/plain", "Expected body: FORMAT");
    return;
  }
  String body = g_http->arg("plain");
  body.trim();
  if (body != "FORMAT") {
    g_http->send(400, "text/plain", "Send body FORMAT to confirm wipe");
    return;
  }
  if (g_sd_fmt.busy) {
    g_http->send(409, "text/plain", "Format already running");
    return;
  }

  PoSer().println("WWW: SD format requested");
  g_sd_fmt.done = false;
  g_sd_fmt.ok = false;
  g_sd_fmt.percent = 0;
  g_sd_fmt.status[0] = '\0';
  g_sd_fmt.message[0] = '\0';
  g_sd_fmt.busy = true;

  const BaseType_t created =
      xTaskCreate(sdFormatTask, "sd_fmt", 12288, nullptr, 1, nullptr);
  if (created != pdPASS) {
    g_sd_fmt.busy = false;
    g_http->send(500, "text/plain", "Could not start format task");
    return;
  }
  g_http->send(202, "application/json", "{\"started\":true}");
}


}  // namespace

bool formatSdIconJson(char* body, size_t body_len) {
  return formatSdIconJsonImpl(body, body_len);
}

void registerSdStatusRoutes(WebServer& http) {
  // SD badge chrome is /api/chrome (formatSdIconJson). Full card UI: /api/sd/status.
  http.on("/api/sd/status", HTTP_GET, handleSdStatusGet);
  http.on("/api/sd/expose", HTTP_POST, handleSdExposePost);
  http.on("/api/sd/expose-boot", HTTP_POST, handleSdExposeBootPost);
  http.on("/api/sd/format", HTTP_POST, handleSdFormatPost);
  http.on("/api/sd/format/progress", HTTP_GET, handleSdFormatProgressGet);
}

}  // namespace remote_hid
