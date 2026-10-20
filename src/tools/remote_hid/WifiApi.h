#pragma once

#include <Arduino.h>
#include <WebServer.h>

namespace remote_hid {

void applyWifi(bool wait_for_sta);
void reloadConfigId(const String& id);
void registerWifiRoutes(WebServer& http);

}  // namespace remote_hid
