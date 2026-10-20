#pragma once

#include <WebServer.h>

namespace lan_scan {

// Register /api/lan/* on the SoftAP/STA web server. Call once after WebServer create.
void registerRoutes(WebServer& http);

// Optional: call from main loop if watch timer needs servicing (also polled via API).
void loop();

}  // namespace lan_scan
