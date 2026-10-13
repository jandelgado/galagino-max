#ifndef ARENA_H
#define ARENA_H

#include <cstdint>

// Bump allocator over two blocks reserved at boot, before the heap
// fragments. Holds either menu logos or the running machine's unpacked
// assets, never both; reset() drops everything on each menu/machine switch.
//
// Two blocks: ESP32 without PSRAM has two internal RAM regions (DRAM,
// D/IRAM). One malloc() cannot span them and neither fits the whole budget.
class Arena {
public:
    // An asset cannot span both blocks, so each block size matters.
    // Menu: 4 equal logo slots only fit as 2 + 2, so a block holds 2 slots.
    // ROM_SLACK: extra space bombjack/1942 need to bin-pack their
    // assets, found by search over real asset sizes. Only one unpack order
    // fits, so these machines unpack eagerly, largest first.
    static constexpr uint32_t LOGO_BLOCK_MIN = 2 * 224 * 96 * sizeof(unsigned short); // 86016, 2 logo slots
    static constexpr uint32_t ROM_SLACK = 4984; // headroom for bombjack/1942 asset shapes
    static constexpr uint32_t BLOCK_A_SIZE = LOGO_BLOCK_MIN + ROM_SLACK; // 91000
    // B takes its whole DRAM region (0x3ffe4350, largest free block 110580
    // at boot). Measured over all games: nothing else allocates there.
    static constexpr uint32_t B_REGION_REST = 19500;
    static constexpr uint32_t BLOCK_B_SIZE = LOGO_BLOCK_MIN + ROM_SLACK + B_REGION_REST; // 110500
    static constexpr uint32_t CAPACITY = BLOCK_A_SIZE + BLOCK_B_SIZE; // 201500

    // Call first in setup(). Aborts if a block can't be reserved.
    static void init();

    static void reset();

    // Called from both cores, see arena.cpp. align is a power of 2:
    // unaligned 16/32 bit access costs cycles in render loops.
    static uint8_t *alloc(uint32_t bytes, uint32_t align = 1);

    // count elements of T, aligned for T
    template<typename T>
    static T *alloc(uint32_t count) {
        return (T *)alloc(count * sizeof(T), alignof(T));
    }

    // diagnostics only
    static uint32_t bytesUsed();

private:
    static uint8_t *blockA;
    static uint8_t *blockB;
    static uint32_t usedA;
    static uint32_t usedB;
};

#endif
