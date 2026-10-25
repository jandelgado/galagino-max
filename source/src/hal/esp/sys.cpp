#include "sys.h"
#ifdef ARDUINO
#include <Arduino.h>
#include <Esp.h>
#include <rom/spi_flash.h>
#else
#include <esp_chip_info.h>
#include <esp_clk_tree.h>
#include <esp_flash.h>
#include <esp_idf_version.h>
#include <sdkconfig.h>
#endif
#include <esp_heap_caps.h>
#include <esp_image_format.h>
#include <esp_ota_ops.h>
#include <esp_random.h>
#include <esp_system.h>
#include <stdio.h>
#include "audio.h"
#include "rtos.h"

#ifdef ARDUINO
// defined in Esp.cpp
uint32_t ESP_getFlashChipId(void);
#endif

namespace hal {

#ifdef ARDUINO

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

#else // ESP-IDF only

namespace {
const char *chipName(esp_chip_model_t model) {
  switch (model) {
    case CHIP_ESP32:   return "ESP32";
    case CHIP_ESP32S2: return "ESP32-S2";
    case CHIP_ESP32S3: return "ESP32-S3";
    case CHIP_ESP32C3: return "ESP32-C3";
    default:           return "ESP32-?";
  }
}
}

void sysInfo(SysInfo &out) {
  esp_chip_info_t info;
  esp_chip_info(&info);
  uint32_t flash = 0;
  esp_flash_get_size(NULL, &flash);
  uint32_t mhz = 0;
  esp_clk_tree_src_get_freq_hz(SOC_MOD_CLK_CPU, ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED, &mhz);
  out.chip = chipName(info.model);
  out.revision = info.revision;
  out.cores = info.cores;
  out.cpuMhz = mhz / 1000000;
  out.flashBytes = flash;
  out.sdkVersion = esp_get_idf_version();
}

MemInfo memInfo() {
  MemInfo m;
  m.free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  m.largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
  m.minFree = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
  return m;
}

#endif // ARDUINO

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

#ifdef ARDUINO

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

#else // ESP-IDF only

// stdout is the IDF console (UART0 / USB, sdkconfig)
void consoleInit() {
#if CONFIG_IDF_TARGET_ESP32S3
  sleepMs(2000); // USB delay
#endif
}

void printPlatformInfo() {
  SysInfo info;
  sysInfo(info);
  printf("ESP-IDF:     %s\n", info.sdkVersion);
  printf("ESP Chip:    %s - %d\n", info.chip, (int)info.revision);
  printf("CPU Clock:   %d MHz, %d cores\n", (int)info.cpuMhz, (int)info.cores);
  printf("Flash Size:  %d MiB\n", (int)(info.flashBytes / 1024 / 1024));
  printf("Free heap: %d\n", (int)memInfo().free);
  printf("Main core: %d\n", xPortGetCoreID());
  printf("Main priority: %d\n", (int)uxTaskPriorityGet(NULL));
}

#endif // ARDUINO

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
