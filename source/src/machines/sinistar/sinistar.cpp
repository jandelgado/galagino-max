#include "sinistar.h"

static_assert(0x0800 + 0x1000 <= RAMSIZE, "Sinistar SRAM in memory[]");

// action key (sinibomb): START with a COIN pin or the galagino
// controller, else EXTRA (defender.cpp)
#if defined(BTN_COIN_PIN) || defined(GALAGINO_CONTROLLER)
static const unsigned int ACTION_KEY = BUTTON_START;
#else
static const unsigned int ACTION_KEY = BUTTON_EXTRA;
#endif

// main ROM unpacks before hw's Arena::alloc calls (member order); every
// order of the three board allocs fits (plan: Arena budget)
sinistar::sinistar()
  : williams(hw),
    sinistar_rom_main(sinistar_rom_main_blob), sinistar_rom_sound(sinistar_rom_sound_blob),
    romMain(sinistar_rom_main.data()), romSound(sinistar_rom_sound.data()),
    hw(Arena::alloc(WilliamsBoard::RAM_SIZE), Arena::alloc(Scanout::BUF_BYTES),
       Arena::alloc(Scanout::BUF_BYTES), romMain, romSound, ACTION_KEY) {}

// SRAM binds after machineBase::reset() cleared memory[], before the CPU runs
void sinistar::reset() {
  williams::reset();
  hw.bindSram(memory + SRAM_OFS);
}
