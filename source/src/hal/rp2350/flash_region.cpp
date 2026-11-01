// Flash writes while the DVI scanout runs. Erase and program stop XIP for
// both cores; the scanout DMA IRQ on the emulation core runs from RAM
// (HAL_FAST_CODE, video_dvi_hstx.cpp) and must keep running, else the TV
// loses sync. pico-sdk's flash_safe_execute masks every IRQ on the other
// core (one resync per write, fine for the Bluetooth keys, not for a
// download). Here:
//
//   video core (caller)                 emulation core
//   give park ------------------------> parker task (top priority) wakes,
//   wait parked <---------------------- BASEPRI 16 in RAM: only priority 0
//   IRQs off, flash_range_*,              (scanout DMA) runs, spin
//   IRQs on, release -----------------> BASEPRI 0, back to sleep
//   wait unparked
#ifdef HAL_RP2350
#include "../../config.h"
#ifdef ASSETS_REMOTE
#include "flash_region.h"
#include <string.h>
#include <FreeRTOS.h>
#include <hardware/flash.h>
#include <hardware/sync.h>
#include <hardware/regs/addressmap.h>
#include "../hal_attr.h"
#include "rtos.h"

// linker: image end, end of the free flash (CMakeLists.txt)
extern "C" char __flash_binary_end;
extern "C" char GALAGINO_FLASH_END;

namespace hal {
namespace {

const uint32_t SECTOR = 4096;
// above every IRQ but the scanout DMA's (PICO_HIGHEST_IRQ_PRIORITY 0)
const uint32_t PARK_BASEPRI = 0x10;

uintptr_t start() {
  return ((uintptr_t)&__flash_binary_end + SECTOR - 1) & ~(uintptr_t)(SECTOR - 1);
}
uintptr_t end() { return (uintptr_t)&GALAGINO_FLASH_END; }

Signal *park;
volatile bool parked;
volatile bool released;

// noinline: inlined into parker() it would run from flash
__attribute__((noinline)) HAL_FAST_CODE void spin() {
  __asm volatile("msr basepri, %0" ::"r"(PARK_BASEPRI) : "memory");
  __dsb();
  parked = true;
  while (!released) {
  }
  parked = false;
  __asm volatile("msr basepri, %0" ::"r"(0) : "memory");
}

void parker(void *) {
  for (;;) {
    park->take(WAIT_FOREVER);
    spin();
  }
}

bool parkerStarted() {
  static bool started;
  if (!started) {
    park = new Signal();
    const uint32_t top = configMAX_PRIORITIES - 1;
    started = taskSpawn(parker, nullptr, "flash park", 512, top, EMULATION_CORE) != nullptr;
  }
  return started;
}

// fn runs every IRQ of this core masked, the other core parked
template <typename Fn>
void withoutXip(Fn fn) {
  released = false;
  park->give();
  while (!parked) {
  }
  const uint32_t irqs = save_and_disable_interrupts();
  fn();
  restore_interrupts(irqs);
  released = true;
  while (parked) {
  }
}

} // namespace

uint32_t FlashRegion::size() const {
  return end() > start() ? end() - start() : 0;
}

bool FlashRegion::erase(uint32_t offset, uint32_t length) {
  if (offset % SECTOR || length % SECTOR || offset + length > size() || !parkerStarted()) {
    return false;
  }
  const uint32_t flashOffset = start() - XIP_BASE + offset;
  for (uint32_t at = 0; at < length; at += SECTOR) {
    withoutXip([&] { flash_range_erase(flashOffset + at, SECTOR); });
    // the video core's tasks (USB, CYW43, display) run in between
    sleepMs(1);
  }
  return true;
}

// whole pages, 0xFF around the data: programming a 1 bit leaves flash as it is
bool FlashRegion::write(uint32_t offset, const uint8_t *data, uint32_t length) {
  if (offset + length > size() || offset + length < offset || !parkerStarted()) {
    return false;
  }
  static uint8_t page[FLASH_PAGE_SIZE];
  while (length) {
    const uint32_t at = offset % FLASH_PAGE_SIZE;
    const uint32_t n = length < FLASH_PAGE_SIZE - at ? length : FLASH_PAGE_SIZE - at;
    memset(page, 0xFF, sizeof(page));
    memcpy(page + at, data, n);
    const uint32_t flashOffset = start() - XIP_BASE + offset - at;
    withoutXip([&] { flash_range_program(flashOffset, page, FLASH_PAGE_SIZE); });
    offset += n;
    data += n;
    length -= n;
  }
  return true;
}

const uint8_t *FlashRegion::map() {
  return size() ? (const uint8_t *)start() : nullptr;
}

} // namespace hal
#endif // ASSETS_REMOTE
#endif // HAL_RP2350
