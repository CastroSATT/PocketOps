#include "tools/lan_scan/LanScanRoutes.h"

#include "http/HttpCtx.h"
#include "storage/LanProfiles.h"
#include "tools/lan_scan/LanScanHttp.h"

namespace lan_scan {
namespace {

bool s_work_routes = false;

}  // namespace

void registerLanHttpRoutes(WebServer& http) {
  // New WebServer instance → work routes must be remounted on next session.
  s_work_routes = false;
  registerLanSessionRoutes(http);
}

void registerLanWorkRoutes(WebServer& http) {
  // Session-core extras + tools (ESP WebServer cannot unregister; gate is the slab).
  http.on("/api/lan/info", HTTP_GET, handleInfoGet);
  http.on("/api/lan/status", HTTP_GET, handleStatusGet);
  http.on("/api/lan/cancel", HTTP_POST, handleCancelPost);
  http.on("/api/lan/watch", HTTP_POST, handleWatchPost);
  http.on("/api/lan/watch", HTTP_GET, handleWatchGet);
  registerLanDiscoverRoutes(http);
  registerLanToolRoutes(http);
  registerLanProfileRoutes(http);
}

bool ensureLanWorkRoutes() {
  if (s_work_routes) {
    return true;
  }
  if (!http::g_http) {
    return false;
  }
  storage::LanProfiles::instance().begin();
  registerLanWorkRoutes(*http::g_http);
  s_work_routes = true;
  return true;
}

}  // namespace lan_scan
