#include "tools/remote_hid/WifiApi.h"
#include "tools/remote_hid/WifiInternal.h"
#include "tools/remote_hid/HttpCtx.h"

namespace remote_hid {

void applyWifi(bool wait_for_sta) { applyWifi_impl(wait_for_sta); }

void reloadConfigId(const String& id) { reloadConfigId_impl(id); }

void registerWifiRoutes(WebServer& http) {
  // Live Wi‑Fi + SD badge chrome is GET /api/chrome (http::sendChromeStatus).
  registerWifiProfileRoutes(http);
  registerWifiScanRoutes(http);
}

}  // namespace remote_hid
