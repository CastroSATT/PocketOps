#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

#include "USBMSC.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"

namespace storage {
namespace tf_internal {

extern USBMSC g_msc;

constexpr size_t kPathBuf = 96;

const char* configuredVolumeLabel();
bool ensureDirEntry(const char* path);
bool ensureDirTree(const char* path);
void makeFatLabel(const char* label, char out[11]);
bool writeVolumeLabel(const char* label);
bool setSdPins(bool mode1bit);
uint32_t sectorCount();
int32_t onRead(uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize);
int32_t onWrite(uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize);
bool onStartStop(uint8_t power, bool start, bool load_eject);
bool tryBegin(bool mode1bit, int freq_khz, bool format_if_failed = false);
void rawDiskClose(sdmmc_card_t* card);
bool rawApplyVolumeLabel();
bool rawFormatFat32(void (*pct)(int, const char*));

}  // namespace tf_internal
}  // namespace storage
