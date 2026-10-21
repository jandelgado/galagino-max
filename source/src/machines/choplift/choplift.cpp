#include "../../catalog.h"
#ifdef GAME_CHOPLIFT

#include "choplift.h"
#include "choplift_logo.h"
#include "choplift_main_rom.h"
#include "choplift_sound_rom.h"
#include "choplift_tiles.h"
#include "choplift_tile_map.h"
#include "choplift_sprites.h"
#include "choplift_lookup_prom.h"
#include "choplift_banks.h"
#include "choplift_palette.h"

// System 2 board (MAME sys2row): PPI, inverted mute bit, 4 banks of 16 KB
// at 8000-bfff (MAME bank0c), palette from the color PROMs. ROT0, full
// 256 px visarea: in play the playfield spans x 8..247 (MAME snapshots),
// so the 240 px window stays at x 8. Sprites, banks and tiles stay PLAIN
// in flash. FIRE shoots, START (or B) turns the helicopter.
static const system1_video::Window CHOPLIFT_WINDOW = {8, 8, system1_video::NO_PLAYER, 0, 8};

choplift::choplift() : system1(Roms{
    choplift_main_rom_blob, choplift_sound_rom_blob, choplift_tiles_blob, choplift_tile_map_blob,
    choplift_sprites_blob, choplift_lookup_prom_blob, nullptr,
    choplift_banks_blob, choplift_palette,
}, Config{system1_video::VISAREA_FULL, true, BOARD_SYS2, ROT0, CHOPLIFT_WINDOW, BANK_0C}) { }

// Power-on (0000 -> 9400): without "OK" at effe the game clears RAM,
// runs a RAM and ROM test (45 s on the real board), stores "OK" and
// restarts, now jumping to the game at 8000. Seed the signature: same
// attract as a cold boot, without the wait.
static const uint16_t WARM_START = 0xeffe - 0xc000;  // memory[] starts at c000

void choplift::reset() {
  system1::reset();
  memory[WARM_START] = 'O';
  memory[WARM_START + 1] = 'K';
}

const Logo &choplift::logo(void) {
  return choplift_logo;
}

#ifdef LED_PIN
// from logos/choplift.png: yellow letters, orange and brown shade
static const CRGB CHOPLIFT_LED_A = CRGB(0xffff50);
static const CRGB CHOPLIFT_LED_B = CRGB(0xe0b040);
static const CRGB CHOPLIFT_LED_C = CRGB(0x90501c);
static const CRGB CHOPLIFT_LED_HIT = CRGB(0xffffff);

void choplift::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[NUM_LEDS] = {
      CHOPLIFT_LED_C, CHOPLIFT_LED_B, CHOPLIFT_LED_A, CHOPLIFT_LED_A,
      CHOPLIFT_LED_A, CHOPLIFT_LED_B, CHOPLIFT_LED_C};
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}

// white while the collision hardware reports a hit, else the menu colors
void choplift::gameLeds(CRGB *leds) {
  if (!collided()) {
    menuLeds(leds);
    return;
  }
  fill_solid(leds, NUM_LEDS, CHOPLIFT_LED_HIT);
}
#endif

#endif // GAME_CHOPLIFT
