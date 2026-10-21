#include "tools/remote_hid/ConfigApi.h"
#include "tools/remote_hid/ConfigApiInternal.h"

namespace remote_hid {

void registerConfigApiRoutes(WebServer& http) {
  registerConfigCrudRoutes(http);
  registerConfigBackupRoutes(http);
  registerDeviceApiRoutes(http);
  registerFirmwareApiRoutes(http);
}

}  // namespace remote_hid
