#include "http/ChromeApi.h"

#include "http/HttpCtx.h"
#include "tools/remote_hid/SdStatusApi.h"
#include "tools/remote_hid/WifiInternal.h"

namespace http {

void sendChromeStatus() {
  char wifi[560];
  char sd[320];
  if (!remote_hid::formatWifiStatusJson(wifi, sizeof(wifi)) ||
      !remote_hid::formatSdIconJson(sd, sizeof(sd))) {
    g_http->send(500, "application/json", "{\"error\":\"oom\"}");
    return;
  }
  char body[900];
  const int n = snprintf(body, sizeof(body), "{\"wifi\":%s,\"sd\":%s}", wifi, sd);
  if (n <= 0 || static_cast<size_t>(n) >= sizeof(body)) {
    g_http->send(500, "application/json", "{\"error\":\"json_overflow\"}");
    return;
  }
  g_http->send(200, "application/json", body);
}

namespace {

void handleChromeGet() { sendChromeStatus(); }

}  // namespace

void registerChromeRoutes(WebServer& http) {
  http.on("/api/chrome", HTTP_GET, handleChromeGet);
}

}  // namespace http
