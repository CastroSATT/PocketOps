#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include "util/JsonWriter.h"

namespace remote_hid {

extern uint8_t g_sta_disc_reason;

void sendJsonArena(util::JsonW& w, char* arena);
void ensureWifiStaEvents();
bool isWrongPasswordReason(uint8_t reason);
bool waitForSta(uint32_t timeout_ms);
void startSoftAp();
bool tryJoinKnownNetworks(uint32_t per_net_ms);
void applyWifi_impl(bool wait_for_sta);
void reloadConfigId_impl(const String& id);
void upsertIniKey(String& body, const char* key, const char* value);
/** Fill a full JSON object `{...}` for Wi‑Fi status. Returns false if truncated. */
bool formatWifiStatusJson(char* body, size_t body_len);
const char* wifiStatusName(wl_status_t st);

void registerWifiProfileRoutes(WebServer& http);
void registerWifiScanRoutes(WebServer& http);

}  // namespace remote_hid
