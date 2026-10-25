#include "sys.h"
#include <Arduino.h>
#include <Esp.h>
#include <esp_heap_caps.h>
#include <esp_image_format.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <rom/spi_flash.h>
#include <stdio.h>
#include "audio.h"
#include "rtos.h"

// defined in Esp.cpp
uint32_t ESP_getFlashChipId(void);

namespace hal {

void sysInfo(SysInfo &out) {
  out.chip = ESP.getChipModel();
  out.revision = ESP.getChipRevision();
  out.cores = ESP.getChipCores();
  out.cpuMhz = ESP.getCpuFreqMHz();
  out.flashBytes = ESP.getFlashChipSize();
  out.sdkVersion = ESP.getSdkVersion();
}

MemInfo memInfo() {
  MemInfo m;
  m.free = ESP.getFreeHeap();
  m.largest = ESP.getMaxAllocHeap();
  m.minFree = ESP.getMinFreeHeap();
  return m;
}

MemInfo memInfoData() {
  MemInfo m;
  m.free = heap_caps_get_free_size(MALLOC_CAP_8BIT);
  m.largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  m.minFree = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
  return m;
}

void memDump() {
  heap_caps_print_heap_info(MALLOC_CAP_8BIT);
}

void *dmaAlloc(uint32_t bytes) {
  return heap_caps_malloc(bytes, MALLOC_CAP_DMA);
}

void consoleInit() {
#if CONFIG_IDF_TARGET_ESP32S3
  sleepMs(2000); // USB delay
#endif
  Serial.begin(115200);
  sleepMs(200); // let serial initialize
}

void printPlatformInfo() {
  printf("ESP-IDF:     %s\n", ESP.getSdkVersion());
  printf("Arduino:     %d.%d.%d\n", ESP_ARDUINO_VERSION_MAJOR, ESP_ARDUINO_VERSION_MINOR, ESP_ARDUINO_VERSION_PATCH);
  printf("ESP Chip:    %s - %d\n", ESP.getChipModel(), ESP.getChipRevision());
  printf("CPU Clock:   %d MHz\n", ESP.getCpuFreqMHz());
  printf("Flash Size:  %d MiB\n", ESP.getFlashChipSize() / 1024 / 1024);
  printf("Flash Speed: %d MHz\n", ESP.getFlashChipSpeed() / 1000 / 1000);
  printf("Flash Mode:  %d - ", ESP.getFlashChipMode());
  switch (ESP.getFlashChipMode()) {
    case FM_QIO:       printf("FM_QIO - Quad IO\n"); break;
    case FM_QOUT:      printf("FM_QOUT - Quad Out\n"); break;
    case FM_DIO:       printf("FM_DIO - Dual IO\n"); break;
    case FM_DOUT:      printf("FM_DOUT - Dual Out\n"); break;
    case FM_FAST_READ: printf("FM_FAST_READ\n"); break;
    case FM_SLOW_READ: printf("FM_SLOW_READ\n"); break;
    default:           printf("FM_UNKNOWN\n"); break;
  }
  printf("Flash Manufacturer: 0x%02x\n", (g_rom_flashchip.device_id >> 16) & 0xff);
  printf("Flash Type Code:    0x%02x\n", (g_rom_flashchip.device_id >> 8) & 0xff);
  printf("Flash Size Code:    0x%02x\n", (g_rom_flashchip.device_id >> 0) & 0xff);
  printf("Flash Id:           0x%08x\n", ESP_getFlashChipId());

#ifdef WORKAROUND_I2S_APLL_PROBLEM
  printf("I2S APLL workaround active\n");
#endif

  printf("Free heap: %d\n", ESP.getFreeHeap());
  printf("Main core: %d\n", xPortGetCoreID());
  printf("Main priority: %d\n", uxTaskPriorityGet(NULL));

#ifdef CONFIG_IDF_TARGET_ESP32
  printf("CONFIG_IDF_TARGET_ESP32:   defined\n");
#endif
#ifdef CONFIG_IDF_TARGET_ESP32S2
  printf("CONFIG_IDF_TARGET_ESP32S2: defined\n");
#endif
#ifdef CONFIG_IDF_TARGET_ESP32S3
  printf("CONFIG_IDF_TARGET_ESP32S3: defined\n");
#endif
}

bool imageVerify(bool &supported) {
  supported = true;
  const esp_partition_t *app = esp_ota_get_running_partition();
  if (!app) {
    return false;
  }

  const esp_partition_pos_t pos = { app->address, app->size };
  esp_image_metadata_t meta;
  return esp_image_verify(ESP_IMAGE_VERIFY_SILENT, &pos, &meta) == ESP_OK;
}

uint32_t random32() {
  return esp_random();
}

bool resetWasPanic() {
  return esp_reset_reason() == ESP_RST_PANIC;
}

} // namespace hal
