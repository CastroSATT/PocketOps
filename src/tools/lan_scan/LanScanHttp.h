#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include "util/JsonWriter.h"

namespace lan_scan {

constexpr size_t kJsonArenaCap = 28672;

String readBody();
String jsonField(const String& body, const char* key);
void jsonEsc(const char* in, char* out, size_t out_len);
bool appendHostsJson(util::JsonW& w);
char* allocJsonArena();
void sendJsonArenaOrError(util::JsonW& w, char* arena);

void handleInfoGet();
void handleStatusGet();
void handleCancelPost();
void handleSessionPost();
void handleWatchPost();
void handleWatchGet();

/** Always-on: open/close session only. */
void registerLanSessionRoutes(WebServer& http);
/** Discover / tools / profiles / info — mounted on first session open. */
void registerLanWorkRoutes(WebServer& http);
void registerLanDiscoverRoutes(WebServer& http);
void registerLanToolRoutes(WebServer& http);
void registerLanProfileRoutes(WebServer& http);
/** Idempotent; call after session buffers are up. */
bool ensureLanWorkRoutes();

}  // namespace lan_scan
