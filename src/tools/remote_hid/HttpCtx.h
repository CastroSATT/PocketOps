#pragma once

#include <WebSocketsServer.h>

#include "http/HttpCtx.h"

namespace remote_hid {

class RemoteHidServer;

// WebServer pointer lives in http::g_http (shared by LAN / SD / logs / config).
using http::g_http;

extern WebSocketsServer* g_ws;
extern int g_http_port;
extern int g_ws_port;
extern RemoteHidServer* g_server;

}  // namespace remote_hid
