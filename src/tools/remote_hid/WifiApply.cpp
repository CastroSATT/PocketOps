#include "tools/remote_hid/WifiApi.h"

#include <WiFi.h>
#include <cstring>

#include "display/Display.h"
#include "led/RgbLed.h"
#include "storage/ConfigStore.h"
#include "storage/DeviceLog.h"
#include "storage/UsbConsole.h"
#include "storage/WifiProfiles.h"
#include "tools/remote_hid/ConfigApi.h"
#include "tools/remote_hid/HttpCtx.h"
#include "tools/remote_hid/WifiInternal.h"
#include "util/JsonEsc.h"
#include "util/JsonWriter.h"

namespace remote_hid {

uint8_t g_sta_disc_reason = 0;

void ensureWifiStaEvents() {
  static bool once = false;
  if (once) {
    return;
  }
  once = true;
  WiFi.onEvent([](arduino_event_id_t event, arduino_event_info_t info) {
    if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
      g_sta_disc_reason = info.wifi_sta_disconnected.reason;
    } else if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
      g_sta_disc_reason = 0;
    }
  });
}

bool isWrongPasswordReason(uint8_t reason) {
  // Wrong PSK usually shows up as 4-way / handshake timeout, not WL_CONNECT_FAILED.
  return reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
         reason == WIFI_REASON_HANDSHAKE_TIMEOUT || reason == WIFI_REASON_AUTH_FAIL ||
         reason == WIFI_REASON_MIC_FAILURE || reason == WIFI_REASON_802_1X_AUTH_FAILED ||
         reason == WIFI_REASON_AUTH_EXPIRE || reason == WIFI_REASON_PAIRWISE_CIPHER_INVALID ||
         reason == WIFI_REASON_GROUP_CIPHER_INVALID || reason == WIFI_REASON_IE_IN_4WAY_DIFFERS;
}

bool waitForSta(uint32_t timeout_ms) {
  ensureWifiStaEvents();
  g_sta_disc_reason = 0;
  const uint32_t start = millis();
  while ((millis() - start) < timeout_ms) {
    if (WiFi.status() == WL_CONNECTED) {
      return true;
    }
    const uint8_t reason = g_sta_disc_reason;
    if (reason == WIFI_REASON_NO_AP_FOUND) {
      return false;
    }
    if (isWrongPasswordReason(reason)) {
      // Brief grace for ESP retries, then fail as wrong password instead of full timeout.
      const uint32_t grace_end = millis() + 2500;
      while (millis() < grace_end && (millis() - start) < timeout_ms) {
        if (WiFi.status() == WL_CONNECTED) {
          return true;
        }
        delay(40);
        yield();
      }
      WiFi.disconnect(false, false);
      return false;
    }
    delay(40);
    yield();
  }
  return WiFi.status() == WL_CONNECTED;
}

void startSoftAp() {
  auto& cfg = storage::ConfigStore::instance();
  WiFi.softAP(cfg.wifiApSsid(), cfg.wifiApPass(), cfg.wifiApChannel());
  PoSer().printf("WIFI: SoftAP %s ch=%d ip=%s\n", cfg.wifiApSsid(), cfg.wifiApChannel(),
                WiFi.softAPIP().toString().c_str());
}

// Scan and try known profiles (strongest first). Also tries last STA from pocketops.ini.
bool tryJoinKnownNetworks(uint32_t per_net_ms) {
  auto& cfg = storage::ConfigStore::instance();
  auto& prof = storage::WifiProfiles::instance();
  prof.load();

  struct Cand {
    int rssi;
    char ssid[33];
    char pass[65];
  };
  // Cap stack candidates (in-range matches); profile store itself holds up to 100.
  constexpr size_t kMaxCands = 24;
  Cand cands[kMaxCands];
  size_t nc = 0;

  auto addCand = [&](const char* ssid, const char* pass, int rssi) {
    if (!ssid || !ssid[0] || nc >= (sizeof(cands) / sizeof(cands[0]))) {
      return;
    }
    for (size_t i = 0; i < nc; ++i) {
      if (strcmp(cands[i].ssid, ssid) == 0) {
        if (rssi > cands[i].rssi) {
          cands[i].rssi = rssi;
        }
        return;
      }
    }
    strncpy(cands[nc].ssid, ssid, sizeof(cands[nc].ssid) - 1);
    cands[nc].ssid[sizeof(cands[nc].ssid) - 1] = '\0';
    strncpy(cands[nc].pass, pass ? pass : "", sizeof(cands[nc].pass) - 1);
    cands[nc].pass[sizeof(cands[nc].pass) - 1] = '\0';
    cands[nc].rssi = rssi;
    ++nc;
  };

  const int n = WiFi.scanNetworks(false, false);
  for (int i = 0; i < n; ++i) {
    const String ssid = WiFi.SSID(i);
    if (!ssid.length()) {
      continue;
    }
    char pass[65] = {};
    if (prof.isAutoJoin(ssid.c_str()) && prof.findPass(ssid.c_str(), pass, sizeof(pass))) {
      addCand(ssid.c_str(), pass, WiFi.RSSI(i));
    } else if (cfg.wifiStaSsid()[0] && ssid == cfg.wifiStaSsid()) {
      // Last-joined network always eligible even if profile auto_join is off.
      addCand(cfg.wifiStaSsid(), cfg.wifiStaPass(), WiFi.RSSI(i));
    }
  }
  WiFi.scanDelete();

  // If scan found nothing known, still try last-used STA + auto-join profiles.
  if (nc == 0) {
    if (cfg.wifiStaSsid()[0]) {
      addCand(cfg.wifiStaSsid(), cfg.wifiStaPass(), -100);
    }
    for (size_t i = 0; i < prof.count(); ++i) {
      if (!prof.autoJoin(i)) {
        continue;
      }
      addCand(prof.ssid(i), prof.pass(i), -120 - static_cast<int>(i));
    }
  }

  // Strongest first.
  for (size_t i = 0; i + 1 < nc; ++i) {
    for (size_t j = i + 1; j < nc; ++j) {
      if (cands[j].rssi > cands[i].rssi) {
        Cand tmp = cands[i];
        cands[i] = cands[j];
        cands[j] = tmp;
      }
    }
  }

  for (size_t i = 0; i < nc; ++i) {
    PoSer().printf("WIFI: try %s (rssi=%d)\n", cands[i].ssid, cands[i].rssi);
    WiFi.disconnect(false, false);
    delay(30);
    WiFi.begin(cands[i].ssid, cands[i].pass);
    if (waitForSta(per_net_ms)) {
      PoSer().printf("WIFI: STA ok %s ip=%s rssi=%d\n", cands[i].ssid,
                    WiFi.localIP().toString().c_str(), WiFi.RSSI());
      return true;
    }
    PoSer().printf("WIFI: fail %s\n", cands[i].ssid);
  }
  return false;
}

void applyWifi_impl(bool wait_for_sta) {
  auto& cfg = storage::ConfigStore::instance();
  auto& prof = storage::WifiProfiles::instance();
  prof.load();

  const char* mode = cfg.wifiMode();
  if (!mode || !mode[0]) {
    mode = "ap";
  }

  const bool mode_ap = (strcmp(mode, "ap") == 0);
  const bool mode_sta = (strcmp(mode, "sta") == 0);
  const bool mode_apsta = (strcmp(mode, "apsta") == 0);
  const bool want_sta = mode_sta || mode_apsta;
  // SoftAP stays up for ap/apsta, and as fallback when sta-only join fails.
  bool want_ap = mode_ap || mode_apsta;

  WiFi.persistent(false);
  WiFi.setAutoReconnect(want_sta);
  WiFi.disconnect(false, false);
  delay(40);

  if (want_ap && want_sta) {
    WiFi.mode(WIFI_AP_STA);
  } else if (want_sta) {
    WiFi.mode(WIFI_STA);
  } else {
    WiFi.mode(WIFI_AP);
  }

  if (want_ap) {
    startSoftAp();
  } else {
    WiFi.softAPdisconnect(true);
  }

  bool joined = false;
  if (want_sta && wait_for_sta) {
    if (want_ap) {
      // Already AP_STA — SoftAP stays while we hunt known networks.
      joined = tryJoinKnownNetworks(8000);
    } else {
      joined = tryJoinKnownNetworks(8000);
      if (!joined) {
        // Fallback: open SoftAP so the page is still reachable for setup.
        PoSer().println("WIFI: STA failed — SoftAP fallback");
        WiFi.mode(WIFI_AP_STA);
        startSoftAp();
        want_ap = true;
      }
    }
  } else if (want_sta && !wait_for_sta && cfg.wifiStaSsid()[0]) {
    WiFi.begin(cfg.wifiStaSsid(), cfg.wifiStaPass());
  }

  if (want_sta && wait_for_sta && !joined) {
    PoSer().println("WIFI: no known network joined (SoftAP available)");
    PO_LOGW("wifi", "STA join failed (mode=%s)", mode);
  } else {
    PO_LOGI("wifi", "applied mode=%s ap=%d sta=%d", mode, want_ap ? 1 : 0, joined ? 1 : 0);
  }
}


}  // namespace remote_hid
