#include "../../catalog.h"
#ifdef GAME_KRULL

#include "krull.h"
#include "krull_rom.h"
#include "krull_gfx.h"

// idle loop "inc ax; test byte [9E8h],0FFh; je $-6" until the NMI handler
// starts a logic frame (every 2nd NMI)
const krull::Board krull::BOARD = {KRULL_RAM_END, 0x1000, 0x6000, 0x1581, 0x1581, 0x09e8, false, 0, 0};

// tile code & 0x7f: MAME's 128 char RAM tiles wrap for codes 0x80-0xff
static constexpr uint8_t CODE_MASK = 0x7f;

// IN1 (active high): service switch active low, kept open
static constexpr uint8_t IN1_SERVICE_OFF = 0x01, IN1_COIN1 = 0x04, IN1_START1 = 0x40;

// IN4: b0-3 right stick (throw), b4-7 left stick (move): up, right, down, left
static constexpr uint8_t IN4_FIRE_UP = 0x01, IN4_FIRE_RIGHT = 0x02, IN4_FIRE_DOWN = 0x04;
static constexpr uint8_t IN4_FIRE_LEFT = 0x08, IN4_MOVE_UP = 0x10, IN4_MOVE_RIGHT = 0x20;
static constexpr uint8_t IN4_MOVE_DOWN = 0x40, IN4_MOVE_LEFT = 0x80;

krull::krull()
  : gottliebMachine(BOARD), krull_main_rom(krull_main_rom_blob), krull_sprites(krull_sprites_blob),
    krull_levels(krull_levels_blob), krull_sound_rom(krull_sound_rom_blob) {
  rom = krull_main_rom.data();
  sprites = krull_sprites.data();
  levels = krull_levels.data();
  charTiles = (uint32_t (*)[8])Arena::alloc(CHAR_TILES * 8 * sizeof(uint32_t), 4);
  vblank = Arena::create<Seqlock<VideoState>>();
  video = Arena::create<VideoState>();
  GottliebSound::Config sc = {};
  sc.rom = krull_sound_rom.data();
  sc.romBase = 0x6000;
  // command wait loop, 84 % of the 6502's instructions in attract (29 cycles/pass):
  //   f13b CLI / LDA $8202 / AND #$40 / BNE f146
  //   f146 SEI / LDA $21 / BNE f153 / LDA #0 / STA $20 / STA $23 / BEQ f13b
  sc.idlePc = 0xf13b;
  sc.idleFlag = 0x21;
  sc.idleZero1 = 0x20;
  sc.idleZero2 = 0x23;
  sc.idlePass = 29;
  sc.dacGain = 19;
  sound.attach(sc);
}

void krull::reset() {
  gottliebMachine::reset();
  stickState = 0;
  sticks = TwinStick();
}

// ---- Inputs ----

uint8_t krull::readDsw(void) {
  return KRULL_DSW | (input->demoSoundsOff() ? KRULL_DSW_DEMO_SOUND_OFF : 0);
}

uint8_t krull::readIn1(unsigned int keys) {
  uint8_t v = IN1_SERVICE_OFF;
  if (keys & BUTTON_COIN) {
    v |= IN1_COIN1;
  }
  if (keys & BUTTON_START) {
    v |= IN1_START1;
  }
  return v;
}

// player view = panel view (ROT270 shown upright): no direction remap
uint8_t krull::readIn4(unsigned int) {
  const uint16_t s = stickState;
  return ((s & TwinStick::FIRE_UP) ? IN4_FIRE_UP : 0) | ((s & TwinStick::FIRE_RIGHT) ? IN4_FIRE_RIGHT : 0) |
         ((s & TwinStick::FIRE_DOWN) ? IN4_FIRE_DOWN : 0) | ((s & TwinStick::FIRE_LEFT) ? IN4_FIRE_LEFT : 0) |
         ((s & TwinStick::MOVE_UP) ? IN4_MOVE_UP : 0) | ((s & TwinStick::MOVE_RIGHT) ? IN4_MOVE_RIGHT : 0) |
         ((s & TwinStick::MOVE_DOWN) ? IN4_MOVE_DOWN : 0) | ((s & TwinStick::MOVE_LEFT) ? IN4_MOVE_LEFT : 0);
}

void krull::pollInputs(void) {
  stickState = sticks.update(input->buttons_get());
}

// ---- LEDs ----

#ifdef LED_PIN
// logo colors: red text, orange-red glaive
static const CRGB KRULL_LED_DARK(0x8c1b13), KRULL_LED_RED(0xd42a20), KRULL_LED_BRIGHT(0xe03f20);

void krull::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[NUM_LEDS] = {
      KRULL_LED_DARK, KRULL_LED_RED, KRULL_LED_BRIGHT, KRULL_LED_BRIGHT,
      KRULL_LED_BRIGHT, KRULL_LED_RED, KRULL_LED_DARK};
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}

// bright while a sound command was sent in the last frame
void krull::gameLeds(CRGB *leds) {
  if (soundCmdSeen) {
    fill_solid(leds, NUM_LEDS, KRULL_LED_BRIGHT);
    return;
  }
  menuLeds(leds);
}
#endif

// ---- Video ----

void krull::publishVideo(void) {
  vblank->publish([this](VideoState &v) {
    latchVideo(v.in);
    memcpy(v.tiles, charTiles, sizeof(v.tiles));
  });
}

void krull::prepare_frame(void) {
  vblank->read(*video);
  gottlieb_video::buildPens(video->in.pal, levels, pens);
  spriteCount = gottlieb_video::listSprites(video->in.spr, spriteList);
}

void krull::render_row(short row) {
  gottlieb_video::renderStrip(row, video->in.vram, video->in.ctrl, pens, video->tiles, CODE_MASK, sprites,
                              spriteList, spriteCount, frame_buffer);
}

#endif
