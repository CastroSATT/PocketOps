#pragma once

#include <WebServer.h>

namespace http {

/** SoftAP / STA WebServer — one pointer for all route modules. */
extern WebServer* g_http;

inline void setServer(WebServer* s) { g_http = s; }

}  // namespace http
