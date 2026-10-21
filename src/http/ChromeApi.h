#pragma once

#include <WebServer.h>

namespace http {

/** Header chrome: Wi‑Fi + SD badge state in one SoftAP hit (`/api/chrome`). */
void registerChromeRoutes(WebServer& http);

/** Send `{"wifi":{...},"sd":{...}}` — shared by GET /api/chrome and Wi‑Fi connect. */
void sendChromeStatus();

}  // namespace http

