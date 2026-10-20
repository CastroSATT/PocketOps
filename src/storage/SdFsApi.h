#pragma once

#include <WebServer.h>

namespace storage {

// Register /api/sd/fs/* file-browser routes on the web server.
void registerSdFsRoutes(WebServer& http);

}  // namespace storage
