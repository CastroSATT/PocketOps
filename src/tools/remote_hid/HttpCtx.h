#pragma once

#include <WebServer.h>
#include <WebSocketsServer.h>

namespace remote_hid {

class RemoteHidServer;

extern WebServer* g_http;
extern WebSocketsServer* g_ws;
extern int g_http_port;
extern int g_ws_port;
extern RemoteHidServer* g_server;

}  // namespace remote_hid
