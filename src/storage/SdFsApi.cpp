#include "storage/SdFsApi.h"

#include <SD_MMC.h>
#include <WiFiClient.h>
#include <cstring>

#include "http/HttpCtx.h"
#include "storage/DeviceLog.h"
#include "storage/TfExpose.h"
#include "util/JsonEsc.h"

namespace storage {
namespace {

using http::g_http;
File g_upload_file;
char g_upload_path[96] = {};
bool g_upload_ok = false;
String g_upload_err;

using util::jsonEscape;

const char* queryPath() {
  if (g_http->hasArg("path")) {
    return g_http->arg("path").c_str();
  }
  return "/";
}

bool requireCardAccess() {
  auto& tf = TfExpose::instance();
  if (!tf.beginExclusive()) {
    const char* err = tf.lastError();
    if (err && strcmp(err, "expose_on") == 0) {
      PO_LOGW("files", "access denied: expose on");
      g_http->send(409, "application/json",
                   "{\"error\":\"expose_on\",\"message\":\"Turn off USB Expose in Config → SD, "
                   "then open Files again.\"}");
      return false;
    }
    if (err && strcmp(err, "sd_settle") == 0) {
      PO_LOGW("files", "access denied: settling after Expose");
      g_http->send(503, "application/json",
                   "{\"error\":\"sd_settle\",\"message\":\"SD is settling after Expose off — eject "
                   "the disk on the Mac if it still shows, wait a second, then try again.\"}");
      return false;
    }
    PO_LOGW("files", "access denied: %s", err && err[0] ? err : "no_sd");
    g_http->send(503, "application/json", "{\"error\":\"no_sd\"}");
    return false;
  }
  tf.noteActivity();
  return true;
}

void handleFilesUiPost() {
  if (!g_http->hasArg("plain")) {
    g_http->send(400, "text/plain", "Expected body: on or off");
    return;
  }
  String body = g_http->arg("plain");
  body.trim();
  body.toLowerCase();
  auto& tf = TfExpose::instance();
  if (body == "on" || body == "1" || body == "true") {
    tf.noteFilesUi(true);
  } else if (body == "off" || body == "0" || body == "false") {
    tf.noteFilesUi(false);
  } else {
    g_http->send(400, "text/plain", "Expected body: on or off");
    return;
  }
  g_http->send(200, "application/json",
               tf.filesUiOpen() ? "{\"files_ui\":true}" : "{\"files_ui\":false}");
}

void handleListGet() {
  auto& tf = TfExpose::instance();
  if (!requireCardAccess()) {
    return;
  }
  String entries;
  const bool ok = tf.listDirectory(queryPath(), entries);
  const char* err = tf.lastError();
  tf.endExclusive();
  if (!ok) {
    char body[160];
    char e[80];
    jsonEscape(err && err[0] ? err : "list fail", e, sizeof(e));
    snprintf(body, sizeof(body), "{\"error\":\"%s\"}", e);
    g_http->send(400, "application/json", body);
    return;
  }
  String json = "{\"path\":\"";
  char pe[120];
  jsonEscape(queryPath(), pe, sizeof(pe));
  json += pe;
  json += "\",\"entries\":";
  json += entries;
  json += '}';
  g_http->send(200, "application/json", json);
}

void handleMkdirPost() {
  auto& tf = TfExpose::instance();
  if (!g_http->hasArg("plain")) {
    g_http->send(400, "application/json", "{\"error\":\"body\"}");
    return;
  }
  String body = g_http->arg("plain");
  String path;
  const int i = body.indexOf("\"path\"");
  if (i >= 0) {
    const int q1 = body.indexOf('"', i + 6);
    const int q2 = body.indexOf('"', q1 + 1);
    if (q1 >= 0 && q2 > q1) {
      path = body.substring(q1 + 1, q2);
    }
  }
  if (!path.length()) {
    g_http->send(400, "application/json", "{\"error\":\"path\"}");
    return;
  }
  if (!requireCardAccess()) {
    return;
  }
  const bool ok = tf.makeDir(path.c_str());
  const char* err = tf.lastError();
  tf.endExclusive();
  if (!ok) {
    char out[160];
    char e[80];
    jsonEscape(err && err[0] ? err : "mkdir", e, sizeof(e));
    snprintf(out, sizeof(out), "{\"error\":\"%s\"}", e);
    g_http->send(400, "application/json", out);
    return;
  }
  g_http->send(200, "application/json", "{\"ok\":true}");
}

void handleDeletePost() {
  auto& tf = TfExpose::instance();
  if (!g_http->hasArg("plain")) {
    g_http->send(400, "application/json", "{\"error\":\"body\"}");
    return;
  }
  String body = g_http->arg("plain");
  String path;
  const int i = body.indexOf("\"path\"");
  if (i >= 0) {
    const int q1 = body.indexOf('"', i + 6);
    const int q2 = body.indexOf('"', q1 + 1);
    if (q1 >= 0 && q2 > q1) {
      path = body.substring(q1 + 1, q2);
    }
  }
  if (!path.length() || path == "/") {
    g_http->send(400, "application/json", "{\"error\":\"path\"}");
    return;
  }
  if (!requireCardAccess()) {
    return;
  }
  const bool ok = tf.removePath(path.c_str());
  const char* err = tf.lastError();
  tf.endExclusive();
  if (!ok) {
    char out[160];
    char e[80];
    jsonEscape(err && err[0] ? err : "delete", e, sizeof(e));
    snprintf(out, sizeof(out), "{\"error\":\"%s\"}", e);
    g_http->send(400, "application/json", out);
    return;
  }
  g_http->send(200, "application/json", "{\"ok\":true}");
}

void handleDownloadGet() {
  auto& tf = TfExpose::instance();
  const char* path = queryPath();
  if (!path || !path[0] || strcmp(path, "/") == 0) {
    g_http->send(400, "text/plain", "path required");
    return;
  }
  if (!requireCardAccess()) {
    return;
  }
  if (tf.isDirectory(path)) {
    tf.endExclusive();
    g_http->send(400, "text/plain", "not a file");
    return;
  }
  File f = tf.openFile(path, FILE_READ);
  if (!f) {
    tf.endExclusive();
    g_http->send(404, "text/plain", "not found");
    return;
  }
  const char* base = strrchr(path, '/');
  base = base ? base + 1 : path;
  // Sanitize filename for Content-Disposition (no quotes / CR).
  char safe_name[96];
  size_t si = 0;
  for (size_t i = 0; base[i] && si + 1 < sizeof(safe_name); ++i) {
    const char c = base[i];
    if (c == '"' || c == '\\' || c == '\r' || c == '\n') {
      continue;
    }
    safe_name[si++] = c;
  }
  safe_name[si] = '\0';
  if (!safe_name[0]) {
    strncpy(safe_name, "download.bin", sizeof(safe_name) - 1);
  }

  const size_t total = f.size();
  WiFiClient cl = g_http->client();
  cl.printf("HTTP/1.1 200 OK\r\n");
  cl.printf("Content-Type: application/octet-stream\r\n");
  cl.printf("Content-Disposition: attachment; filename=\"%s\"\r\n", safe_name);
  cl.printf("Content-Length: %u\r\n", static_cast<unsigned>(total));
  cl.printf("Connection: close\r\n\r\n");
  // Chunked body with yields — avoids WDT hangs after MSC teardown.
  uint8_t buf[1024];
  size_t sent = 0;
  while (f.available() && cl.connected()) {
    const int n = f.read(buf, sizeof(buf));
    if (n <= 0) {
      break;
    }
    size_t off = 0;
    while (off < static_cast<size_t>(n) && cl.connected()) {
      const size_t w = cl.write(buf + off, static_cast<size_t>(n) - off);
      if (w == 0) {
        delay(1);
        yield();
        continue;
      }
      off += w;
      sent += w;
    }
    yield();
  }
  f.close();
  tf.endExclusive();
  PO_LOGI("files", "download %s (%u/%u bytes)", path, static_cast<unsigned>(sent),
          static_cast<unsigned>(total));
}

void handleUploadWrite() {
  HTTPUpload& upload = g_http->upload();
  auto& tf = TfExpose::instance();
  if (upload.status == UPLOAD_FILE_START) {
    g_upload_ok = false;
    g_upload_err = "";
    if (g_upload_file) {
      g_upload_file.close();
    }
    String dir = g_http->hasArg("path") ? g_http->arg("path") : "/";
    if (!dir.length() || dir[0] != '/') {
      dir = "/";
    }
    while (dir.length() > 1 && dir.endsWith("/")) {
      dir.remove(dir.length() - 1);
    }
    String name = upload.filename;
    const int slash = name.lastIndexOf('/');
    if (slash >= 0) {
      name = name.substring(slash + 1);
    }
    // Sanitize filename (no path segments / odd chars that confuse FAT).
    name.replace('\\', '_');
    name.replace('/', '_');
    name.replace(':', '_');
    if (!name.length()) {
      g_upload_err = "no filename";
      return;
    }
    if (!tf.beginExclusive()) {
      const char* err = tf.lastError();
      g_upload_err = (err && err[0]) ? err : "no_sd";
      return;
    }
    if (dir == "/") {
      snprintf(g_upload_path, sizeof(g_upload_path), "/%s", name.c_str());
    } else {
      snprintf(g_upload_path, sizeof(g_upload_path), "%s/%s", dir.c_str(), name.c_str());
    }
    // Remove existing file first so FILE_WRITE is a clean replace.
    if (tf.pathExists(g_upload_path) && !tf.isDirectory(g_upload_path)) {
      tf.removePath(g_upload_path);
    }
    g_upload_file = tf.openFile(g_upload_path, FILE_WRITE);
    if (!g_upload_file) {
      g_upload_err = "open fail";
      tf.endExclusive();
      return;
    }
    g_upload_ok = true;
    yield();
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (g_upload_ok && g_upload_file) {
      tf.noteActivity();
      const size_t n = upload.currentSize;
      size_t off = 0;
      while (off < n) {
        const size_t chunk = (n - off > 1024) ? 1024 : (n - off);
        const size_t w = g_upload_file.write(upload.buf + off, chunk);
        if (w != chunk) {
          g_upload_ok = false;
          g_upload_err = "write fail";
          g_upload_file.close();
          if (tf.exclusiveHeld()) {
            tf.endExclusive();
          }
          return;
        }
        off += w;
        yield();  // keep Wi‑Fi / USB / watchdog alive on large JPEGs
      }
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (g_upload_file) {
      g_upload_file.flush();
      g_upload_file.close();
    }
    if (g_upload_ok) {
      tf.noteActivity();
    }
    if (tf.exclusiveHeld()) {
      tf.endExclusive();
    }
    yield();
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    if (g_upload_file) {
      g_upload_file.close();
    }
    g_upload_ok = false;
    g_upload_err = "aborted";
    if (tf.exclusiveHeld()) {
      tf.endExclusive();
    }
  }
}

void handleUploadPost() {
  if (!g_upload_ok || g_upload_err.length()) {
    if (g_upload_err == "expose_on") {
      g_http->send(409, "application/json",
                   "{\"error\":\"expose_on\",\"message\":\"Turn off USB Expose in Config → SD, "
                   "then upload again.\"}");
      return;
    }
    char body[160];
    char e[80];
    jsonEscape(g_upload_err.length() ? g_upload_err.c_str() : "upload fail", e, sizeof(e));
    snprintf(body, sizeof(body), "{\"error\":\"%s\"}", e);
    g_http->send(400, "application/json", body);
    return;
  }
  char path_e[160];
  jsonEscape(g_upload_path, path_e, sizeof(path_e));
  char body[200];
  snprintf(body, sizeof(body), "{\"ok\":true,\"path\":\"%s\"}", path_e);
  g_http->send(200, "application/json", body);
}

}  // namespace

void registerSdFsRoutes(WebServer& http) {
  http::setServer(&http);
  http.on("/api/sd/files-ui", HTTP_POST, handleFilesUiPost);
  http.on("/api/sd/fs/list", HTTP_GET, handleListGet);
  http.on("/api/sd/fs/mkdir", HTTP_POST, handleMkdirPost);
  http.on("/api/sd/fs/delete", HTTP_POST, handleDeletePost);
  http.on("/api/sd/fs/download", HTTP_GET, handleDownloadGet);
  http.on("/api/sd/fs/upload", HTTP_POST, handleUploadPost, handleUploadWrite);
}

}  // namespace storage
