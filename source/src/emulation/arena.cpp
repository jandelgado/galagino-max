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
uint32_t Arena::limitA = Arena::BLOCK_A_SIZE;
uint32_t Arena::limitB = Arena::BLOCK_B_SIZE;
uint32_t Arena::plannedA = Arena::BLOCK_A_SIZE;
uint32_t Arena::plannedB = Arena::BLOCK_B_SIZE;

void Arena::init() {
#ifdef ASSETS_REMOTE
  // B first, so it gets its region before A takes it (smaller boot heap)
  blockB = (uint8_t *)malloc(BLOCK_B_SIZE);
  blockA = (uint8_t *)malloc(BLOCK_A_SIZE);
#else
  blockA = (uint8_t *)malloc(BLOCK_A_SIZE);
  blockB = (uint8_t *)malloc(BLOCK_B_SIZE);
#endif
  if (!blockA || !blockB) {
    printf("Arena: boot-time reservation failed (A=%p B=%p)\n", (void *)blockA, (void *)blockB);
    abort();
  }
}

void Arena::reset() {
  CriticalSection guard;
  usedA = 0;
  usedB = 0;
  limitA = plannedA = BLOCK_A_SIZE;
  limitB = plannedB = BLOCK_B_SIZE;
}

void Arena::reserve(const ArenaPlan *plan) {
  CriticalSection guard;
  usedA = 0;
  usedB = 0;
  const uint32_t a = plan ? plan->a : 0;
  const uint32_t b = plan ? plan->b : 0;
  if (a > BLOCK_A_SIZE || b > BLOCK_B_SIZE) {
    printf("fatal: arena reserve %u + %u exceeds blocks\n", (unsigned)a, (unsigned)b);
    abort();
  }
  limitA = plannedA = BLOCK_A_SIZE - a;
  limitB = plannedB = BLOCK_B_SIZE - b;
}

// padding to align block + used, computed on the address, not the offset
static uint32_t pad(const uint8_t *block, uint32_t used, uint32_t align) {
  return (uint32_t)(-(uintptr_t)(block + used) & (align - 1));
}

// bump `used` inside [used, end) of block, nullptr if it does not fit
static uint8_t *bump(uint8_t *block, uint32_t &used, uint32_t end, uint32_t bytes, uint32_t align) {
  const uint32_t start = used + pad(block, used, align);
  if (start + bytes > end) {
    return nullptr;
  }
  used = start + bytes;
  return block + start;
}

uint8_t *Arena::alloc(uint32_t bytes, uint32_t align, ArenaBlock block) {
  CriticalSection guard;

  if (block != ARENA_ANY) {
    uint8_t *p = block == ARENA_A ? bump(blockA, plannedA, BLOCK_A_SIZE, bytes, align)
                                  : bump(blockB, plannedB, BLOCK_B_SIZE, bytes, align);
    if (!p) {
      printf("fatal: arena: block %c reservation exceeded, wanted=%u\n",
             block == ARENA_A ? 'A' : 'B', (unsigned)bytes);
      abort();
    }
    return p;
  }

  uint8_t *p = bump(blockA, usedA, limitA, bytes, align);
  if (!p) {
    p = bump(blockB, usedB, limitB, bytes, align);
  }
  if (p) {
    return p;
  }
  printf("fatal: arena out of memory, wanted=%d, left=%d\n", bytes, bytesUsed());
  abort();
  return nullptr;
}

uint32_t Arena::bytesUsed() {
  CriticalSection guard;
  return usedA + usedB + (plannedA - limitA) + (plannedB - limitB);
}
