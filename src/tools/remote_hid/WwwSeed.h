#pragma once

#include <Arduino.h>
#include <WebServer.h>

namespace remote_hid {

bool seedWwwIfMissing();
bool loadPage(String& out);
void handleWwwAssetGet();
void registerWwwRoutes(WebServer& http);

}  // namespace remote_hid
