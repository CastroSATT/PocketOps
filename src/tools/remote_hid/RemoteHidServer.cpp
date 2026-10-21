#include "tools/remote_hid/RemoteHidServer.h"

#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <cstring>

#include "Branding.h"
#include "display/Display.h"
#include "hid/HidProtocol.h"
#include "hid/UsbHid.h"
#include "led/RgbLed.h"
#include "http/HttpApi.h"
#include "storage/ConfigStore.h"
#include "storage/DeviceLog.h"
#include "storage/UsbConsole.h"
#include "tools/lan_scan/LanScan.h"
#include "tools/remote_hid/HttpCtx.h"
#include "tools/remote_hid/WifiApi.h"
#include "tools/remote_hid/WwwSeed.h"

namespace remote_hid {
namespace {

void onWsEvent(uint8_t /*num*/, WStype_t type, uint8_t* payload, size_t length) {
  if (!g_server) {
    return;
  }

  if (type == WStype_CONNECTED) {
    g_server->setClientCount(static_cast<uint8_t>(g_server->clientCount() + 1));
  } else if (type == WStype_DISCONNECTED) {
    const uint8_t n = g_server->clientCount();
    if (n > 0) {
      g_server->setClientCount(static_cast<uint8_t>(n - 1));
    }
  } else if (type == WStype_BIN && payload && length > 0) {
    hid::HidProtocol::processBinary(payload, length);
  } else if (type == WStype_TEXT && payload && length > 0) {
    char buf[256];
    const size_t n = length < sizeof(buf) - 1 ? length : sizeof(buf) - 1;
    memcpy(buf, payload, n);
    buf[n] = '\0';
    hid::HidProtocol::processMessage(buf);
  }
}

}  // namespace

void RemoteHidServer::begin() {
  if (running_) {
    return;
  }

  // Config is web-only; App already loaded INIs at boot / after web save.
  auto& cfg = storage::ConfigStore::instance();
  display::applyFromConfig();
  led::RgbLed::instance().setBrightnessPercent(cfg.lightBrightness());

  if (!g_http || g_http_port != cfg.webPort()) {
    delete g_http;
    g_http = new WebServer(cfg.webPort());
    g_http_port = cfg.webPort();
  }
  if (!g_ws || g_ws_port != cfg.wsPort()) {
    delete g_ws;
    g_ws = new WebSocketsServer(cfg.wsPort());
    g_ws_port = cfg.wsPort();
  }

  applyWifi(true);

  seedWwwIfMissing();
  PO_LOGI("web", "http=%u ws=%u", static_cast<unsigned>(cfg.webPort()),
          static_cast<unsigned>(cfg.wsPort()));

  g_http->on("/", HTTP_GET, []() {
    String page;
    if (!loadPage(page)) {
      g_http->send(500, "text/plain", "Missing /www/index.html");
      return;
    }
    auto& c = storage::ConfigStore::instance();
    page.replace("__PO_REMOTE_TITLE__", branding::kRemotePageTitle);
    page.replace("__PO_WS_PORT__", String(c.wsPort()));
    g_http->send(200, "text/html", page);
  });
  http::registerPhoneApi(*g_http);
  g_http->onNotFound([]() {
    const String uri = g_http->uri();
    if (uri.startsWith("/css/") || uri.startsWith("/js/")) {
      handleWwwAssetGet();
      return;
    }
    g_http->send(404, "text/plain", "Not found");
  });
  g_http->begin();

  g_server = this;
  clients_ = 0;
  g_ws->begin();
  g_ws->onEvent(onWsEvent);

  hid::UsbHid::instance().begin();
  running_ = true;
}

void RemoteHidServer::loop() {
  if (!running_ || !g_http || !g_ws) {
    return;
  }
  g_http->handleClient();
  g_ws->loop();
  lan_scan::loop();
}

void RemoteHidServer::end() {
  if (!running_) {
    return;
  }
  if (g_ws) {
    g_ws->close();
  }
  if (g_http) {
    g_http->stop();
  }
  WiFi.disconnect(true, false);
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  g_server = nullptr;
  clients_ = 0;
  running_ = false;
}

IPAddress RemoteHidServer::apIp() const { return WiFi.softAPIP(); }

IPAddress RemoteHidServer::staIp() const {
  if (WiFi.status() == WL_CONNECTED) {
    return WiFi.localIP();
  }
  return IPAddress();
}

IPAddress RemoteHidServer::pageIp() const {
  if (WiFi.status() == WL_CONNECTED) {
    return WiFi.localIP();
  }
  return WiFi.softAPIP();
}

}  // namespace remote_hid
