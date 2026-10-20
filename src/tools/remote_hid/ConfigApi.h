#pragma once

#include <Arduino.h>
#include <WebServer.h>

namespace remote_hid {

bool readTextFile(const char* path, String& out);
bool writeTextFile(const char* path, const String& body);
void registerConfigApiRoutes(WebServer& http);

}  // namespace remote_hid
