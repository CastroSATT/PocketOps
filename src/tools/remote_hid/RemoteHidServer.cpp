#include "tools/remote_hid/RemoteHidServer.h"

#include <WebServer.h>
#include <WebSocketsServer.h>
#include <WiFi.h>
#include <cstring>

#include "config.h"
#include "hid/HidProtocol.h"
#include "hid/UsbHid.h"
#include "tools/remote_hid/RemoteHidPage.h"

namespace {

WebServer http(config::WEB_PORT);
WebSocketsServer ws(config::WS_PORT);
remote_hid::RemoteHidServer* g_server = nullptr;

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
  } else if (type == WStype_TEXT && payload && length > 0) {
    char buf[256];
    const size_t n = length < sizeof(buf) - 1 ? length : sizeof(buf) - 1;
    memcpy(buf, payload, n);
    buf[n] = '\0';
    hid::HidProtocol::processMessage(buf);
  }
}

}  // namespace

namespace remote_hid {

void RemoteHidServer::begin() {
  if (running_) {
    return;
  }

  WiFi.mode(WIFI_AP);
  WiFi.softAP(config::WIFI_AP_SSID, config::WIFI_AP_PASS, config::WIFI_AP_CHANNEL);

  http.on("/", HTTP_GET, []() { http.send_P(200, "text/html", REMOTE_HID_PAGE); });
  http.onNotFound([]() { http.send(404, "text/plain", "Not found"); });
  http.begin();

  g_server = this;
  clients_ = 0;
  ws.begin();
  ws.onEvent(onWsEvent);

  hid::UsbHid::instance().begin();
  running_ = true;
}

void RemoteHidServer::loop() {
  if (!running_) {
    return;
  }
  http.handleClient();
  ws.loop();
}

void RemoteHidServer::end() {
  if (!running_) {
    return;
  }
  ws.close();
  http.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  g_server = nullptr;
  clients_ = 0;
  running_ = false;
}

IPAddress RemoteHidServer::apIp() const { return WiFi.softAPIP(); }

}  // namespace remote_hid
