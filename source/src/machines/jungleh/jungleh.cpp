#include "../../catalog.h"
#ifdef GAME_JUNGLEH

#include "jungleh.h"
#include "jungleh_dipswitches.h"
#include "jungleh_logo.h"
#include "jungleh_main_rom.h"
#include "jungleh_sound_rom.h"
#include "jungleh_gfx_rom.h"
#include "jungleh_draw_order.h"

// Idle loops (MAME dasm), 65 % main and 97 % sound of all host instructions:
// main 0d0d ei / 0d11 nop x4 / jr $0d11: the game runs in its vblank IRQ
// sound 00bf ld hl,$800e / ld ($4800),hl / ld hl,$000f / ld ($4800),hl
//       (DAC idle level, volume 0) / 00cb ld a,($4013) / and a /
//       jp m,$00d4 / jr $00bf: waits for the NMI handler to set bit 7.
//       Skipped from 00cb on, after the DAC writes of the pass.
static constexpr uint16_t JUNGLEH_MAIN_IDLE_PC = 0x0d11;
static constexpr uint16_t JUNGLEH_SOUND_IDLE_PC = 0x00cb, JUNGLEH_SOUND_IDLE_FLAG = 0x0013;

jungleh::jungleh()
  : elevator(jungleh_main_rom_blob, jungleh_sound_rom_blob, jungleh_gfx_rom_blob,
             jungleh_draw_order_blob,
             Variant{JUNGLEH_MAIN_IDLE_PC, IRQ_ONLY, JUNGLEH_SOUND_IDLE_PC,
                     JUNGLEH_SOUND_IDLE_FLAG, 0x80, JUNGLEH_DSW1, JUNGLEH_DSW2, JUNGLEH_DSW3,
                     ROT180, JUMP_NONE}) { }

const Logo &jungleh::logo(void) {
  return jungleh_logo;
}

#ifdef LED_PIN
// logo colors (logos/jungleh.png): white letters
static const CRGB JUNGLEH_LED_WHITE(0xffffff), JUNGLEH_LED_DARK(0x303030);

void jungleh::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[NUM_LEDS] = {
      JUNGLEH_LED_DARK, JUNGLEH_LED_WHITE, JUNGLEH_LED_WHITE, JUNGLEH_LED_WHITE,
      JUNGLEH_LED_WHITE, JUNGLEH_LED_WHITE, JUNGLEH_LED_DARK};
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}

// bright while FIRE (jump, stab) is held
void jungleh::gameLeds(CRGB *leds) {
  if (input->buttons_get() & BUTTON_FIRE) {
    fill_solid(leds, NUM_LEDS, JUNGLEH_LED_WHITE);
    return;
  }
  menuLeds(leds);
}
#endif

#endif
