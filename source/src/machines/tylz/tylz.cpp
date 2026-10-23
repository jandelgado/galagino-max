#include "../../catalog.h"
#ifdef GAME_TYLZ

#include "tylz.h"
#include "tylz_rom.h"
#include "tylz_gfx.h"

// boot ROM checksum, 32768 passes of "mov dl,[si]; add ax,dx; inc si; loop":
// about 35 8088 cycles per pass (MAME: 14 NMIs), 4 + 5 budget units, so the
// "ROM n PASSES" screens last as long as on the board and the game keeps
// MAME's frame phase afterwards (test_tylz_boot lockstep: 4 or 6 drift)
static constexpr uint16_t CHECKSUM_LOOP = 0x9e13;
static constexpr uint8_t CHECKSUM_COST = 5;

// frame wait: "cmp byte [0C11h],0; je $" until the NMI handler sets the flag
const tylz::Board tylz::BOARD = {TYLZ_RAM_END, 0x8000, 0x8000, 0x8aae, 0x8aae, 0x0c11, false,
                                    CHECKSUM_LOOP, CHECKSUM_COST};

// IN1 (active high): service switch active low, kept open
static constexpr uint8_t IN1_SERVICE_OFF = 0x01, IN1_COIN1 = 0x08;

// IN4 (active high): 8-way stick, button 1, starts; b7 unknown dip, default open
static constexpr uint8_t IN4_DOWN = 0x01, IN4_RIGHT = 0x02, IN4_UP = 0x04, IN4_LEFT = 0x08;
static constexpr uint8_t IN4_FIRE = 0x10, IN4_START1 = 0x20, IN4_START2 = 0x40, IN4_DIP = 0x80;

tylz::tylz()
  : gottliebMachine(BOARD), tylz_main_rom(tylz_main_rom_blob), tylz_tiles(tylz_tiles_blob),
    tylz_sprites(tylz_sprites_blob), tylz_levels(tylz_levels_blob), tylz_sound_rom(tylz_sound_rom_blob),
    tylz_sc01(tylz_sc01_blob) {
  rom = tylz_main_rom.data();
  tiles = tylz_tiles.data();
  sprites = tylz_sprites.data();
  levels = tylz_levels.data();
  vblank = Arena::create<Seqlock<GottliebVideoInput>>();
  video = Arena::create<GottliebVideoInput>();
  GottliebSound::Config sc = {};
  sc.rom = tylz_sound_rom.data();
  sc.romBase = 0x7000;
  sc.sc01a = tylz_sc01.data();
  sc.table = Arena::alloc(VotraxSc01::TABLE_BYTES, 4);
  // command wait loop, byte for byte Q*bert's (29 cycles/pass):
  //   7035 CLI / LDA #0 / STA $20 / STA $23 / LDA $8202 / AND #$40 / BNE 7046
  //   7046 SEI / LDA $21 / BNE 704d / BEQ 7035
  sc.idlePc = 0x7035;
  sc.idleFlag = 0x21;
  sc.idleZero1 = 0x20;
  sc.idleZero2 = 0x23;
  sc.idlePass = 29;
  sc.dacGain = 4;
  sound.attach(sc);
}

// ---- Inputs ----

uint8_t tylz::readDsw(void) {
  return input->demoSoundsOff() ? TYLZ_DSW & ~TYLZ_DSW_DEMO_SOUND_ON : TYLZ_DSW;
}

uint8_t tylz::readIn1(unsigned int keys) {
  return IN1_SERVICE_OFF | ((keys & BUTTON_COIN) ? IN1_COIN1 : 0);
}

// player view = panel view (ROT0 shown upright): no direction remap
uint8_t tylz::readIn4(unsigned int keys) {
  return IN4_DIP | ((keys & BUTTON_DOWN) ? IN4_DOWN : 0) | ((keys & BUTTON_RIGHT) ? IN4_RIGHT : 0) |
         ((keys & BUTTON_UP) ? IN4_UP : 0) | ((keys & BUTTON_LEFT) ? IN4_LEFT : 0) |
         ((keys & BUTTON_FIRE) ? IN4_FIRE : 0) | ((keys & BUTTON_START) ? IN4_START1 : 0) |
         ((keys & BUTTON_B) ? IN4_START2 : 0);   // B: galagino controller only
}

// ---- LEDs ----

#ifdef LED_PIN
void tylz::menuLeds(CRGB *leds) {
  fill_solid(leds, NUM_LEDS, CRGB::Black);
}

void tylz::gameLeds(CRGB *leds) {
  menuLeds(leds);
}
#endif

// ---- Video ----

void tylz::publishVideo(void) {
  vblank->publish([this](GottliebVideoInput &v) { latchVideo(v); });
}

void tylz::prepare_frame(void) {
  vblank->read(*video);
  gottlieb_video::buildPens(video->pal, levels, pens);
  spriteCount = gottlieb_video::listSpritesRot0(video->spr, spriteList);
}

void tylz::render_row(short row) {
  gottlieb_video::renderStripRot0(row, video->vram, video->ctrl, pens, tiles, sprites, spriteList, spriteCount,
                                  frame_buffer);
}

#endif
