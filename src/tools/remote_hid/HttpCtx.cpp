#include "tools/remote_hid/HttpCtx.h"

namespace remote_hid {

WebServer* g_http = nullptr;
WebSocketsServer* g_ws = nullptr;
int g_http_port = -1;
int g_ws_port = -1;
RemoteHidServer* g_server = nullptr;

}  // namespace remote_hid
