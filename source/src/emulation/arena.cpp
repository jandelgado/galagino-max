#include "arena.h"
#include <cstdio>
#include <cstdlib>

// Both cores allocate: the emulation task unpacks ROMs on first CPU access,
// the render task unpacks tiles/sprites on first draw. Needs a lock.
#ifdef ARDUINO
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

namespace {
portMUX_TYPE arenaMux = portMUX_INITIALIZER_UNLOCKED;

class CriticalSection {
public:
  CriticalSection() { portENTER_CRITICAL(&arenaMux); }
  ~CriticalSection() { portEXIT_CRITICAL(&arenaMux); }
};
}
#else
namespace {
class CriticalSection { };
}
#endif

uint8_t *Arena::blockA = nullptr;
uint8_t *Arena::blockB = nullptr;
uint32_t Arena::usedA = 0;
uint32_t Arena::usedB = 0;

void Arena::init() {
  blockA = (uint8_t *)malloc(BLOCK_A_SIZE);
  blockB = (uint8_t *)malloc(BLOCK_B_SIZE);
  if (!blockA || !blockB) {
    printf("Arena: boot-time reservation failed (A=%p B=%p)\n", (void *)blockA, (void *)blockB);
    abort();
  }
}

void Arena::reset() {
  CriticalSection guard;
  usedA = 0;
  usedB = 0;
}

// padding to align block + used, computed on the address, not the offset
static uint32_t pad(const uint8_t *block, uint32_t used, uint32_t align) {
  return (uint32_t)(-(uintptr_t)(block + used) & (align - 1));
}

uint8_t *Arena::alloc(uint32_t bytes, uint32_t align) {
  CriticalSection guard;

  uint32_t start = usedA + pad(blockA, usedA, align);
  if (start + bytes <= BLOCK_A_SIZE) {
    usedA = start + bytes;
    return blockA + start;
  }

  start = usedB + pad(blockB, usedB, align);
  if (start + bytes <= BLOCK_B_SIZE) {
    usedB = start + bytes;
    return blockB + start;
  }
  printf("fatal: arena out of memory, wanted=%d, left=%d\n", bytes, bytesUsed());
  abort();
  return nullptr;
}

uint32_t Arena::bytesUsed() {
  CriticalSection guard;
  return usedA + usedB;
}
