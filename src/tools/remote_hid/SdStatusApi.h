#pragma once

#include <stddef.h>
#include <WebServer.h>

namespace remote_hid {

/** Fill a full JSON object `{...}` for the header SD badge. */
bool formatSdIconJson(char* body, size_t body_len);
void registerSdStatusRoutes(WebServer& http);

}  // namespace remote_hid
