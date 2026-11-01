/* Added to the pico-sdk linker script (CMakeLists.txt). The Arena is
   malloc'd at boot from the heap between the end of .bss and the stacks;
   HAL_HOT_CODE of every game in the image sits in .data and shrinks it.
   Fail the link instead of an abort at boot. */
ASSERT(__HeapLimit - __end__ >= GALAGINO_ARENA_BYTES + GALAGINO_HEAP_RESERVE,
       "galagino: heap too small for the Arena, build fewer games per image")
