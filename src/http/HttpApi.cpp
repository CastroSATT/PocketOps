#include "http/HttpApi.h"

#include "http/ChromeApi.h"
#include "storage/DeviceLog.h"
#include "storage/SdFsApi.h"
#include "tools/lan_scan/LanScan.h"
#include "tools/remote_hid/ConfigApi.h"
#include "tools/remote_hid/SdStatusApi.h"
#include "tools/remote_hid/WifiApi.h"
#include "tools/remote_hid/WwwSeed.h"

namespace http {

void registerPhoneApi(WebServer& http) {
  remote_hid::registerWwwRoutes(http);
  remote_hid::registerConfigApiRoutes(http);
  remote_hid::registerWifiRoutes(http);
  remote_hid::registerSdStatusRoutes(http);
  registerChromeRoutes(http);
  storage::registerSdFsRoutes(http);
  storage::registerDeviceLogRoutes(http);
  lan_scan::registerRoutes(http);
}

}  // namespace http
