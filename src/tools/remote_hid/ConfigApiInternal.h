#pragma once

#include <Arduino.h>
#include <WebServer.h>

namespace remote_hid {

const char* pathForConfigId(const String& id);
bool ensureParentDir(const char* path);
String configIdFromUri();

void registerConfigCrudRoutes(WebServer& http);
void registerConfigBackupRoutes(WebServer& http);
void registerDeviceApiRoutes(WebServer& http);
void registerFirmwareApiRoutes(WebServer& http);

}  // namespace remote_hid
