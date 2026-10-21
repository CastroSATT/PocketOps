#pragma once

#include <WebServer.h>

namespace http {

/** Mount all SoftAP/STA phone APIs (www assets, config, wifi, SD, LAN, logs). */
void registerPhoneApi(WebServer& http);

}  // namespace http
