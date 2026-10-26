#ifndef ARENA_LAYOUT_H
#define ARENA_LAYOUT_H

// Arena block sizes per layout (arena.h). Read by romconv
// (romconv/romconv/plan/layout.py): plain integer literals only.
// ASSETS_REMOTE builds link WiFi (~23 KB static DRAM out of the boot heap),
// so their block A is smaller.
// ARENA_A_LOCAL: HEAP_PROBE over all azdelivery-local games leaves >= 10 KB
// of A's heap region free (LZMA fallback malloc needs ~5.3 KB). The heap
// hands out blocks this large in 4 KB steps: keep it a multiple of 4096.
#define ARENA_A_LOCAL 118784
#define ARENA_A_REMOTE 69000
#define ARENA_B_SIZE 110500

#endif
