#pragma once

#include <WebServer.h>

namespace lan_scan {

// Register LAN front desk (/api/lan/session). Full /api/lan/* mounts on session open.
void registerRoutes(WebServer& http);

// Optional: call from main loop if watch timer needs servicing (also polled via API).
void loop();

// LAN tab session: host tables live only while acquired (see /api/lan/session).
bool acquireSession();
void releaseSession();

}  // namespace lan_scan
