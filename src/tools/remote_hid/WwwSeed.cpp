#include "tools/remote_hid/WwwSeed.h"

#include <FS.h>
#include <LittleFS.h>
#include <cstring>

#include "Branding.h"
#include "generated/RemoteHidWwwSeed.h"
#include "storage/ConfigStore.h"
#include "storage/UsbConsole.h"
#include "tools/remote_hid/HttpCtx.h"

namespace remote_hid {
namespace {

constexpr const char* kPagePath = "/www/index.html";

bool ensureDir(const char* path) {
  if (LittleFS.exists(path)) {
    File f = LittleFS.open(path);
    const bool is_dir = f && f.isDirectory();
    if (f) {
      f.close();
    }
    if (is_dir) {
      return true;
    }
    LittleFS.remove(path);
  }
  return LittleFS.mkdir(path);
}

bool ensureWwwDir() { return ensureDir("/www"); }

bool ensureWwwParents(const char* fs_path) {
  // fs_path like /www/css/app.css → mkdir /www and /www/css
  if (!fs_path || strncmp(fs_path, "/www/", 5) != 0) {
    return false;
  }
  if (!ensureWwwDir()) {
    return false;
  }
  const char* slash = strrchr(fs_path, '/');
  if (!slash || slash <= fs_path + 4) {
    return true;
  }
  char parent[96];
  const size_t n = static_cast<size_t>(slash - fs_path);
  if (n >= sizeof(parent)) {
    return false;
  }
  memcpy(parent, fs_path, n);
  parent[n] = '\0';
  if (strcmp(parent, "/www") == 0) {
    return true;
  }
  // One nesting level under /www (css|js) is enough for this UI.
  return ensureDir(parent);
}

bool seedWwwFile(const WwwSeedFile& file) {
  if (LittleFS.exists(file.path)) {
    return true;
  }
  if (!ensureWwwParents(file.path)) {
    PoSer().printf("WWW: mkdir parents fail %s\n", file.path);
    return false;
  }
  File f = LittleFS.open(file.path, "w");
  if (!f) {
    PoSer().printf("WWW: create fail %s\n", file.path);
    return false;
  }
  f.print(FPSTR(file.data));
  const size_t n = f.size();
  f.close();
  PoSer().printf("WWW: seeded %s (%u bytes)\n", file.path, static_cast<unsigned>(n));
  return true;
}

bool seedWwwIfMissing_impl() {
  bool ok = true;
  for (size_t i = 0; i < WWW_SEED_FILE_COUNT; ++i) {
    if (!seedWwwFile(WWW_SEED_FILES[i])) {
      ok = false;
    }
  }
  return ok;
}

const char* mimeForWwwPath(const char* path) {
  if (!path) {
    return "text/plain";
  }
  if (strstr(path, ".css")) {
    return "text/css";
  }
  if (strstr(path, ".js")) {
    return "application/javascript";
  }
  if (strstr(path, ".html")) {
    return "text/html";
  }
  return "text/plain";
}

bool wwwAssetUriToFsPath(const String& uri, char* out, size_t out_len) {
  // Allow only /css/* and /js/* → /www/css/* and /www/js/*
  if (!(uri.startsWith("/css/") || uri.startsWith("/js/"))) {
    return false;
  }
  if (uri.indexOf("..") >= 0 || uri.indexOf('\\') >= 0) {
    return false;
  }
  for (size_t i = 0; i < uri.length(); ++i) {
    const char c = uri[i];
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                    c == '/' || c == '.' || c == '_' || c == '-';
    if (!ok) {
      return false;
    }
  }
  const size_t need = 5 + uri.length();  // "/www" + uri + NUL
  if (need >= out_len) {
    return false;
  }
  snprintf(out, out_len, "/www%s", uri.c_str());
  return true;
}

void handleWwwAssetGet_impl() {
  char fs_path[96];
  if (!wwwAssetUriToFsPath(g_http->uri(), fs_path, sizeof(fs_path))) {
    g_http->send(404, "text/plain", "Not found");
    return;
  }
  seedWwwIfMissing();
  if (!LittleFS.exists(fs_path)) {
    g_http->send(404, "text/plain", "Not found");
    return;
  }
  File f = LittleFS.open(fs_path, "r");
  if (!f) {
    g_http->send(500, "text/plain", "Open failed");
    return;
  }
  g_http->streamFile(f, mimeForWwwPath(fs_path));
  f.close();
}

bool loadPage_impl(String& out) {
  if (!seedWwwIfMissing()) {
    return false;
  }
  File f = LittleFS.open(kPagePath, "r");
  if (!f) {
    PoSer().printf("WWW: open fail %s\n", kPagePath);
    return false;
  }
  out = f.readString();
  f.close();
  if (out.length() == 0) {
    PoSer().println("WWW: empty page — re-seeding");
    LittleFS.remove(kPagePath);
    // Force re-seed of index only.
    for (size_t i = 0; i < WWW_SEED_FILE_COUNT; ++i) {
      if (strcmp(WWW_SEED_FILES[i].path, kPagePath) == 0) {
        if (!seedWwwFile(WWW_SEED_FILES[i])) {
          return false;
        }
        break;
      }
    }
    f = LittleFS.open(kPagePath, "r");
    if (!f) {
      return false;
    }
    out = f.readString();
    f.close();
  }
  return out.length() > 0;
}


}  // namespace

bool seedWwwIfMissing() {
  return seedWwwIfMissing_impl();
}

bool loadPage(String& out) {
  return loadPage_impl(out);
}

void handleWwwAssetGet() {
  handleWwwAssetGet_impl();
}

void registerWwwRoutes(WebServer& http) {
  http.on("/css/app.css", HTTP_GET, handleWwwAssetGet);
}

}  // namespace remote_hid
