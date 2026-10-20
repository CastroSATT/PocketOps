#include "storage/DeviceLog.h"

#include <LittleFS.h>
#include <Preferences.h>
#include <SD_MMC.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "storage/ConfigStore.h"
#include "storage/TfExpose.h"
#include "storage/UsbConsole.h"

namespace storage {
namespace {

constexpr const char* kDir = "/logs";
constexpr const char* kIndex = "/logs/index.txt";
WebServer* g_http = nullptr;

const char* levelStr(DeviceLog::Level level) {
  switch (level) {
    case DeviceLog::Level::Warn:
      return "WARN";
    case DeviceLog::Level::Error:
      return "ERROR";
    default:
      return "INFO";
  }
}

void trimLine(char* s) {
  if (!s) {
    return;
  }
  size_t n = strlen(s);
  while (n && (s[n - 1] == '\r' || s[n - 1] == '\n' || s[n - 1] == ' ' || s[n - 1] == '\t')) {
    s[--n] = '\0';
  }
}

}  // namespace

DeviceLog& DeviceLog::instance() {
  static DeviceLog s;
  return s;
}

bool DeviceLog::ensureDir() {
  if (LittleFS.exists(kDir)) {
    return true;
  }
  return LittleFS.mkdir(kDir);
}

bool DeviceLog::validName(const char* name) const {
  if (!name || !name[0] || strlen(name) >= kMaxNameLen) {
    return false;
  }
  if (strchr(name, '/') || strchr(name, '\\') || strstr(name, "..")) {
    return false;
  }
  for (const char* p = name; *p; ++p) {
    const char c = *p;
    const bool ok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                    c == '.' || c == '_' || c == '-';
    if (!ok) {
      return false;
    }
  }
  const size_t n = strlen(name);
  return n > 4 && strcmp(name + n - 4, ".log") == 0;
}

void DeviceLog::pathFor(const char* name, char* out, size_t out_len) const {
  snprintf(out, out_len, "%s/%s", kDir, name);
}

const char* DeviceLog::nameAt(size_t i) const {
  return i < count_ ? names_[i] : "";
}

size_t DeviceLog::sizeAt(size_t i) const {
  if (i >= count_) {
    return 0;
  }
  char path[64];
  pathFor(names_[i], path, sizeof(path));
  File f = LittleFS.open(path, "r");
  if (!f) {
    return 0;
  }
  const size_t n = f.size();
  f.close();
  return n;
}

bool DeviceLog::loadIndex() {
  count_ = 0;
  File f = LittleFS.open(kIndex, "r");
  if (!f) {
    return true;
  }
  while (f.available() && count_ < kMaxFiles) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (!line.length()) {
      continue;
    }
    if (!validName(line.c_str())) {
      continue;
    }
    char path[64];
    pathFor(line.c_str(), path, sizeof(path));
    if (!LittleFS.exists(path)) {
      continue;
    }
    strncpy(names_[count_], line.c_str(), kMaxNameLen - 1);
    names_[count_][kMaxNameLen - 1] = '\0';
    ++count_;
  }
  f.close();
  return true;
}

bool DeviceLog::saveIndex() {
  File f = LittleFS.open(kIndex, "w");
  if (!f) {
    return false;
  }
  for (size_t i = 0; i < count_; ++i) {
    f.println(names_[i]);
  }
  f.close();
  return true;
}

void DeviceLog::evictOldest() {
  while (count_ >= kMaxFiles) {
    char path[64];
    pathFor(names_[0], path, sizeof(path));
    LittleFS.remove(path);
    for (size_t i = 1; i < count_; ++i) {
      memcpy(names_[i - 1], names_[i], kMaxNameLen);
    }
    --count_;
  }
}

void DeviceLog::mirrorToSd(const char* name) {
  if (!name || !validName(name)) {
    return;
  }
  auto& tf = TfExpose::instance();
  if (tf.enabled() || tf.exclusiveHeld()) {
    return;
  }
  if (!tf.cardPresent() && !tf.mounted()) {
    return;
  }
  char path[64];
  pathFor(name, path, sizeof(path));
  File src = LittleFS.open(path, "r");
  if (!src) {
    return;
  }
  String body;
  body.reserve(src.size() + 8);
  while (src.available()) {
    body += static_cast<char>(src.read());
  }
  src.close();
  char sd_path[80];
  snprintf(sd_path, sizeof(sd_path), "/Logs/Device/%s", name);
  if (tf.writeTextFile(sd_path, body.c_str())) {
    // best-effort
  }
}

void DeviceLog::startNewSession() {
  // Prefer wall clock if RTC/NTP looks sane; else millis-based name.
  char name[kMaxNameLen];
  const time_t now = time(nullptr);
  struct tm tm_now;
  bool named = false;
  if (now > 1700000000 && localtime_r(&now, &tm_now)) {
    named = snprintf(name, sizeof(name), "%04d%02d%02d_%02d%02d%02d.log", tm_now.tm_year + 1900,
                     tm_now.tm_mon + 1, tm_now.tm_mday, tm_now.tm_hour, tm_now.tm_min,
                     tm_now.tm_sec) > 0;
  }
  if (!named) {
    snprintf(name, sizeof(name), "boot_%010lu.log", static_cast<unsigned long>(millis()));
  }

  // Avoid collision if begin() called twice quickly.
  char path[64];
  pathFor(name, path, sizeof(path));
  if (LittleFS.exists(path)) {
    snprintf(name, sizeof(name), "boot_%010lu_%02u.log", static_cast<unsigned long>(millis()),
             static_cast<unsigned>(count_ % 100));
    pathFor(name, path, sizeof(path));
  }

  if (current_[0]) {
    mirrorToSd(current_);
  }

  while (count_ >= kMaxFiles) {
    evictOldest();
  }

  File f = LittleFS.open(path, "w");
  if (!f) {
    current_[0] = '\0';
    current_bytes_ = 0;
    return;
  }
  f.printf("# PocketOps session start ms=%lu\n", static_cast<unsigned long>(millis()));
  current_bytes_ = f.size();
  f.close();

  strncpy(names_[count_], name, kMaxNameLen - 1);
  names_[count_][kMaxNameLen - 1] = '\0';
  ++count_;
  strncpy(current_, name, kMaxNameLen - 1);
  current_[kMaxNameLen - 1] = '\0';
  saveIndex();
}

void DeviceLog::loadEnabled() {
  Preferences prefs;
  enabled_ = true;
  if (prefs.begin(ConfigStore::instance().nvsNamespace(), true)) {
    enabled_ = prefs.getBool("log_on", true);
    prefs.end();
  }
}

void DeviceLog::persistEnabled() {
  Preferences prefs;
  if (!prefs.begin(ConfigStore::instance().nvsNamespace(), false)) {
    return;
  }
  prefs.putBool("log_on", enabled_);
  prefs.end();
}

void DeviceLog::stopSession() {
  if (current_[0]) {
    mirrorToSd(current_);
  }
  current_[0] = '\0';
  current_bytes_ = 0;
}

void DeviceLog::setEnabled(bool on) {
  if (on == enabled_) {
    return;
  }
  enabled_ = on;
  persistEnabled();
  if (!ready_) {
    return;
  }
  if (enabled_) {
    startNewSession();
    PoSer().printf("LOG: enabled → %s\n", current_);
  } else {
    stopSession();
    PoSer().println("LOG: disabled (no new writes)");
  }
}

void DeviceLog::begin() {
  ready_ = false;
  current_[0] = '\0';
  current_bytes_ = 0;
  loadEnabled();
  if (!ensureDir()) {
    PoSer().println("LOG: mkdir /logs failed");
    return;
  }
  loadIndex();
  // Drop index entries for missing files already handled in loadIndex.
  // If more than max from older firmware, trim.
  while (count_ > kMaxFiles) {
    evictOldest();
  }
  saveIndex();
  ready_ = true;
  if (enabled_) {
    startNewSession();
    PoSer().printf("LOG: session %s (%u/%u)\n", current_, static_cast<unsigned>(count_),
                  static_cast<unsigned>(kMaxFiles));
  } else {
    PoSer().println("LOG: logging off (files still viewable)");
  }
}

void DeviceLog::maybeRotateBySize() {
  if (!ready_ || !current_[0] || current_bytes_ < kMaxFileBytes) {
    return;
  }
  char path[64];
  pathFor(current_, path, sizeof(path));
  File f = LittleFS.open(path, "a");
  if (f) {
    f.println("# rotated — file size limit");
    f.close();
  }
  startNewSession();
}

void DeviceLog::appendLine(const char* line) {
  if (!enabled_ || !ready_ || !current_[0] || !line) {
    return;
  }
  char path[64];
  pathFor(current_, path, sizeof(path));
  File f = LittleFS.open(path, "a");
  if (!f) {
    return;
  }
  f.println(line);
  current_bytes_ = f.size();
  f.close();
  maybeRotateBySize();
}

void DeviceLog::vlog(Level level, const char* tag, const char* fmt, va_list ap) {
  char msg[kMaxLineLen];
  vsnprintf(msg, sizeof(msg), fmt ? fmt : "", ap);
  trimLine(msg);

  const char* lvl = levelStr(level);
  const char* t = (tag && tag[0]) ? tag : "-";
  // USB console (if enabled); LittleFS only when device logging is on.
  PoSer().printf("%s [%s] %s\n", lvl, t, msg);

  if (!enabled_ || !ready_) {
    return;
  }
  char line[kMaxLineLen + 48];
  snprintf(line, sizeof(line), "%lu %s %s %s", static_cast<unsigned long>(millis()), lvl, t, msg);
  appendLine(line);
}

void DeviceLog::log(Level level, const char* tag, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vlog(level, tag, fmt, ap);
  va_end(ap);
}

void DeviceLog::info(const char* tag, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vlog(Level::Info, tag, fmt, ap);
  va_end(ap);
}

void DeviceLog::warn(const char* tag, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vlog(Level::Warn, tag, fmt, ap);
  va_end(ap);
}

void DeviceLog::error(const char* tag, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vlog(Level::Error, tag, fmt, ap);
  va_end(ap);
}

bool DeviceLog::removeAt(size_t index) {
  if (index >= count_) {
    return false;
  }
  if (strcmp(names_[index], current_) == 0) {
    return false;  // do not delete the active session
  }
  char path[64];
  pathFor(names_[index], path, sizeof(path));
  LittleFS.remove(path);
  for (size_t i = index + 1; i < count_; ++i) {
    memcpy(names_[i - 1], names_[i], kMaxNameLen);
  }
  --count_;
  saveIndex();
  return true;
}

bool DeviceLog::removeNamed(const char* name) {
  if (!validName(name)) {
    return false;
  }
  for (size_t i = 0; i < count_; ++i) {
    if (strcmp(names_[i], name) == 0) {
      return removeAt(i);
    }
  }
  return false;
}

bool DeviceLog::clearAll() {
  // Keep current session; wipe the rest.
  char keep[kMaxNameLen];
  strncpy(keep, current_, kMaxNameLen - 1);
  keep[kMaxNameLen - 1] = '\0';
  for (size_t i = 0; i < count_; ++i) {
    if (strcmp(names_[i], keep) == 0) {
      continue;
    }
    char path[64];
    pathFor(names_[i], path, sizeof(path));
    LittleFS.remove(path);
  }
  count_ = 0;
  if (keep[0]) {
    strncpy(names_[0], keep, kMaxNameLen - 1);
    names_[0][kMaxNameLen - 1] = '\0';
    count_ = 1;
  }
  saveIndex();
  return true;
}

File DeviceLog::openRead(const char* name) {
  if (!validName(name)) {
    return File();
  }
  bool known = false;
  for (size_t i = 0; i < count_; ++i) {
    if (strcmp(names_[i], name) == 0) {
      known = true;
      break;
    }
  }
  if (!known) {
    return File();
  }
  char path[64];
  pathFor(name, path, sizeof(path));
  return LittleFS.open(path, "r");
}

// --- HTTP ---

void handleLogsListGet() {
  auto& log = DeviceLog::instance();
  auto& ser = UsbConsole::instance();
  String json = "{\"enabled\":";
  json += log.enabled() ? "true" : "false";
  json += ",\"serial\":";
  json += ser.enabled() ? "true" : "false";
  json += ",\"max\":";
  json += static_cast<unsigned>(DeviceLog::kMaxFiles);
  json += ",\"current\":\"";
  json += log.currentName();
  json += "\",\"files\":[";
  // Newest first for UI.
  for (size_t n = log.count(); n > 0; --n) {
    const size_t i = n - 1;
    if (n != log.count()) {
      json += ',';
    }
    json += "{\"name\":\"";
    json += log.nameAt(i);
    json += "\",\"size\":";
    json += static_cast<unsigned>(log.sizeAt(i));
    json += ",\"current\":";
    json += (strcmp(log.nameAt(i), log.currentName()) == 0) ? "true" : "false";
    json += '}';
  }
  json += "]}";
  g_http->send(200, "application/json", json);
}

void handleLogsEnablePost() {
  if (!g_http->hasArg("plain")) {
    g_http->send(400, "text/plain", "Expected body: on or off");
    return;
  }
  String body = g_http->arg("plain");
  body.trim();
  body.toLowerCase();
  auto& log = DeviceLog::instance();
  if (body == "on" || body == "1" || body == "true") {
    log.setEnabled(true);
  } else if (body == "off" || body == "0" || body == "false") {
    log.setEnabled(false);
  } else {
    g_http->send(400, "text/plain", "Expected body: on or off");
    return;
  }
  char out[64];
  snprintf(out, sizeof(out), "{\"ok\":true,\"enabled\":%s,\"serial\":%s}",
           log.enabled() ? "true" : "false",
           UsbConsole::instance().enabled() ? "true" : "false");
  g_http->send(200, "application/json", out);
}

void handleLogsSerialPost() {
  if (!g_http->hasArg("plain")) {
    g_http->send(400, "text/plain", "Expected body: on or off");
    return;
  }
  String body = g_http->arg("plain");
  body.trim();
  body.toLowerCase();
  auto& ser = UsbConsole::instance();
  if (body == "on" || body == "1" || body == "true") {
    ser.setEnabled(true);
  } else if (body == "off" || body == "0" || body == "false") {
    ser.setEnabled(false);
  } else {
    g_http->send(400, "text/plain", "Expected body: on or off");
    return;
  }
  char out[64];
  snprintf(out, sizeof(out), "{\"ok\":true,\"serial\":%s,\"enabled\":%s}",
           ser.enabled() ? "true" : "false",
           DeviceLog::instance().enabled() ? "true" : "false");
  g_http->send(200, "application/json", out);
}

void handleLogsFileGet() {
  if (!g_http->hasArg("name")) {
    g_http->send(400, "text/plain", "name required");
    return;
  }
  const String name = g_http->arg("name");
  File f = DeviceLog::instance().openRead(name.c_str());
  if (!f) {
    g_http->send(404, "text/plain", "not found");
    return;
  }
  g_http->streamFile(f, "text/plain");
  f.close();
}

void handleLogsDeletePost() {
  if (!g_http->hasArg("plain")) {
    g_http->send(400, "text/plain", "Expected body: name=… or clear");
    return;
  }
  String body = g_http->arg("plain");
  body.trim();
  auto& log = DeviceLog::instance();
  if (body.equalsIgnoreCase("clear") || body == "all") {
    log.clearAll();
    g_http->send(200, "application/json", "{\"ok\":true,\"cleared\":true}");
    return;
  }
  String name = body;
  if (name.startsWith("name=")) {
    name = name.substring(5);
  }
  name.trim();
  if (!log.removeNamed(name.c_str())) {
    g_http->send(400, "text/plain", "cannot delete (missing or current session)");
    return;
  }
  g_http->send(200, "application/json", "{\"ok\":true}");
}

void registerDeviceLogRoutes(WebServer& http) {
  g_http = &http;
  http.on("/api/logs", HTTP_GET, handleLogsListGet);
  http.on("/api/logs/file", HTTP_GET, handleLogsFileGet);
  http.on("/api/logs/delete", HTTP_POST, handleLogsDeletePost);
  http.on("/api/logs/enable", HTTP_POST, handleLogsEnablePost);
  http.on("/api/logs/serial", HTTP_POST, handleLogsSerialPost);
}

}  // namespace storage
