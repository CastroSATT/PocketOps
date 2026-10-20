#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include "tools/lan_scan/LanScanState.h"

namespace lan_scan {

void mergeNotesFromProfile();
void autoSaveProfile();
void loadProfileIntoHosts();
bool startJob(TaskArgs* ta);

}  // namespace lan_scan
