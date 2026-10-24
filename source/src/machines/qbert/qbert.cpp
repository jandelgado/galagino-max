#include "../../catalog.h"
#ifdef GAME_QBERT

#include "qbert.h"
#include "qbert_rom.h"
#include "qbert_gfx.h"

// frame wait: "cmp ax,[0x80]; je $" until the NMI handler counts the frame
// (service mode has a copy)
const qbert::Board qbert::BOARD = {QBERT_RAM_END, 0xa000, 0xa000, 0xb36d, 0xdb77, 0x0080, true, 0, 0};

// IN1 (active high): service switch active low, kept open
static constexpr uint8_t IN1_START1 = 0x01, IN1_START2 = 0x02, IN1_COIN1 = 0x04;
static constexpr uint8_t IN1_SERVICE_OFF = 0x40;

// IN4 joystick, mounted turned 45 degrees: each switch is a screen diagonal
static constexpr uint8_t STICK_DOWN_RIGHT = 0x01, STICK_UP_LEFT = 0x02;
static constexpr uint8_t STICK_UP_RIGHT = 0x04, STICK_DOWN_LEFT = 0x08;

qbert::qbert()
  : gottliebMachine(BOARD), qbert_main_rom(qbert_main_rom_blob), qbert_tiles(qbert_tiles_blob),
    qbert_sprites(qbert_sprites_blob), qbert_levels(qbert_levels_blob), qbert_sound_rom(qbert_sound_rom_blob),
    qbert_sc01(qbert_sc01_blob) {
  rom = qbert_main_rom.data();
  tiles = qbert_tiles.data();
  sprites = qbert_sprites.data();
  levels = qbert_levels.data();
  vblank = Arena::create<Seqlock<GottliebVideoInput>>();
  video = Arena::create<GottliebVideoInput>();
  GottliebSound::Config sc = {};
  sc.rom = qbert_sound_rom.data();
  sc.romBase = 0x7000;
  sc.sc01a = qbert_sc01.data();
  sc.table = Arena::alloc(VotraxSc01::TABLE_BYTES, 4);
  sc.idlePc = 0x7035;
  sc.idleFlag = 0x21;
  sc.idleZero1 = 0x20;
  sc.idleZero2 = 0x23;
  sc.idlePass = 29;
  sc.dacGain = 4;
  sound.attach(sc);
}

// ---- Inputs ----

uint8_t qbert::readDsw(void) {
  return QBERT_DSW | (input->demoSoundsOff() ? QBERT_DSW_DEMO_SOUND_OFF : 0);
}

uint8_t qbert::readIn1(unsigned int keys) {
  uint8_t v = IN1_SERVICE_OFF;
  if (keys & BUTTON_COIN) {
    v |= IN1_COIN1;
  }
  if (keys & BUTTON_START) {
    v |= IN1_START1;
  }
  if (keys & BUTTON_B) {
    v |= IN1_START2;   // galagino controller only
  }
  return v;
}

// A diagonal maps to its switch; a single direction is turned 45 degrees
// clockwise (up = up-right, right = down-right, ...)
uint8_t qbert::readIn4(unsigned int keys) {
  const bool up = keys & BUTTON_UP, down = keys & BUTTON_DOWN;
  const bool left = keys & BUTTON_LEFT, right = keys & BUTTON_RIGHT;
  if (up) {
    return left ? STICK_UP_LEFT : STICK_UP_RIGHT;
  }
  if (down) {
    return right ? STICK_DOWN_RIGHT : STICK_DOWN_LEFT;
  }
  if (right) {
    return STICK_DOWN_RIGHT;
  }
  return left ? STICK_UP_LEFT : 0;
}

// ---- LEDs ----

#ifdef LED_PIN
// logo colors: title yellow, Q*bert orange and red
static const CRGB QBERT_LED_YELLOW(0xdede00), QBERT_LED_ORANGE(0xc6712b), QBERT_LED_RED(0xef1021);
static constexpr uint8_t OUT_KNOCKER = 0x20;

void qbert::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[NUM_LEDS] = {
      QBERT_LED_YELLOW, QBERT_LED_ORANGE, QBERT_LED_RED, QBERT_LED_RED,
      QBERT_LED_RED, QBERT_LED_ORANGE, QBERT_LED_YELLOW};
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}

// red while the knocker (Q*bert falls off the pyramid) is driven
void qbert::gameLeds(CRGB *leds) {
  if (outputLatch & OUT_KNOCKER) {
    fill_solid(leds, NUM_LEDS, QBERT_LED_RED);
    return;
  }
  menuLeds(leds);
}
#endif

// ---- Video ----

void qbert::publishVideo(void) {
  vblank->publish([this](GottliebVideoInput &v) { latchVideo(v); });
}

void qbert::prepare_frame(void) {
  vblank->read(*video);
  gottlieb_video::buildPens(video->pal, levels, pens);
  spriteCount = gottlieb_video::listSprites(video->spr, spriteList);
}

void qbert::render_row(short row) {
  gottlieb_video::renderStrip(row, video->vram, video->ctrl, pens, tiles, 0xff, sprites, spriteList, spriteCount,
                              frame_buffer);
}

#endif
