#ifndef ARENA_LAYOUT_H
#define ARENA_LAYOUT_H

// Arena block sizes per layout (arena.h). Read by romconv
// (pyconv/arena_layout.py): plain integer literals only.
// ASSETS_REMOTE builds link WiFi (~23 KB static DRAM out of the boot heap),
// so their block A is smaller.
#define ARENA_A_LOCAL 91000
#define ARENA_A_REMOTE 69000
#define ARENA_B 110500

#endif
