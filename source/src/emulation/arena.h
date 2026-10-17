#ifndef ARENA_H
#define ARENA_H

#include <cstdint>
#include "arena_layout.h"

// Where an asset unpacks: ANY = first fit (A, then B); A/B = that block's
// reserved region, planned per game by romconv (<game>_arena_plan.h).
enum ArenaBlock : uint8_t { ARENA_ANY = 0, ARENA_A = 1, ARENA_B = 2 };

// Bytes per block a game's planned assets need, multiples of 4.
struct ArenaPlan {
    uint32_t a;
    uint32_t b;
};

// Bump allocator over two blocks reserved at boot, before the heap
// fragments. Holds either menu logos or the running machine's unpacked
// assets, never both; reset() drops everything on each menu/machine switch.
//
// Two blocks: ESP32 without PSRAM has two internal RAM regions (DRAM,
// D/IRAM). One malloc() cannot span them and neither fits the whole budget.
class Arena {
public:
    // An asset cannot span both blocks, so each block size matters.
    // GAME_BLOCK_MIN: the size both blocks had when they were sized for 2
    // RGB565 menu logos each. Games bin-pack their assets against it; the
    // menu now needs 43136 B (4 indexed logos) plus the drum table, in A.
    // ROM_SLACK: extra space bombjack, 1942 and starforce/baluba need to
    // bin-pack their assets, found by search over real asset sizes. Not
    // every unpack order fits bombjack and 1942, so they unpack eagerly,
    // largest first.
    static constexpr uint32_t GAME_BLOCK_MIN = 86016;
    static constexpr uint32_t ROM_SLACK = 4984; // headroom for bombjack/1942 asset shapes
    // Sizes live in arena_layout.h, shared with romconv.
#ifdef ASSETS_REMOTE
    // WiFi links ~23 KB static DRAM, taken from the boot heap
    static constexpr uint32_t BLOCK_A_SIZE = ARENA_A_REMOTE;
#else
    static constexpr uint32_t BLOCK_A_SIZE = ARENA_A_LOCAL;
#endif
    // B takes its whole DRAM region (0x3ffe4350, largest free block 110580
    // at boot). Measured over all games: nothing else allocates there.
    static constexpr uint32_t B_REGION_REST = 19500;
    static constexpr uint32_t BLOCK_B_SIZE = ARENA_B_SIZE;
    static constexpr uint32_t CAPACITY = BLOCK_A_SIZE + BLOCK_B_SIZE; // 201500

    // Call first in setup(). Aborts if a block can't be reserved.
    static void init();

    // Empties the Arena and drops the reservation.
    static void reset();

    // Empties the Arena and keeps the top plan->a / plan->b bytes of each
    // block for planned assets, so the result does not depend on unpack
    // order. nullptr: no reservation.
    static void reserve(const ArenaPlan *plan);

    // Called from both cores, see arena.cpp. align is a power of 2:
    // unaligned 16/32 bit access costs cycles in render loops.
    // ARENA_ANY: first fit below the reservations. ARENA_A/B: inside that
    // block's reservation, aborts if it does not fit.
    static uint8_t *alloc(uint32_t bytes, uint32_t align = 1, ArenaBlock block = ARENA_ANY);

    // count elements of T, aligned for T
    template<typename T>
    static T *alloc(uint32_t count) {
        return (T *)alloc(count * sizeof(T), alignof(T));
    }

    // diagnostics only
    static uint32_t bytesUsed();

private:
    static_assert(ARENA_A_LOCAL == GAME_BLOCK_MIN + ROM_SLACK, "arena_layout.h: A");
    static_assert(ARENA_B_SIZE == GAME_BLOCK_MIN + ROM_SLACK + B_REGION_REST, "arena_layout.h: B");

    static uint8_t *blockA;
    static uint8_t *blockB;
    static uint32_t usedA;
    static uint32_t usedB;
    // reservation: planned region [limit, size), its bump offset
    static uint32_t limitA;
    static uint32_t limitB;
    static uint32_t plannedA;
    static uint32_t plannedB;
};

#endif
