#include "../../catalog.h"
#ifdef GAME_QBERT

#include "qbert.h"
#include "qbert_rom.h"
#include "qbert_gfx.h"

// frame wait: "cmp ax,[0x80]; je $" until the NMI handler counts the frame
// (service mode has a copy)
static constexpr uint16_t FRAME_WAIT = 0xb36d, FRAME_WAIT_SERVICE = 0xdb77;
static constexpr uint16_t FRAME_COUNTER = 0x0080;

// I/O 5800-5fff (mirror 0x07f8), gottlieb_base_map
static constexpr uint16_t IO_BASE = 0x5800;
static constexpr uint8_t IO_DSW = 0, IO_IN1 = 1, IO_IN2 = 2, IO_IN3 = 3, IO_IN4 = 4;
static constexpr uint8_t IO_WATCHDOG = 0, IO_SOUND = 2, IO_OUTPUT = 3;
static constexpr uint16_t SPRITE_BASE = 0x3000, VRAM_BASE = 0x3800, CHAR_BASE = 0x4000;
static constexpr uint16_t PALETTE_BASE = 0x5000;

// IN1 (active high): service switch active low, kept open
static constexpr uint8_t IN1_START1 = 0x01, IN1_START2 = 0x02, IN1_COIN1 = 0x04;
static constexpr uint8_t IN1_SERVICE_OFF = 0x40;

// IN4 joystick, mounted turned 45 degrees: each switch is a screen diagonal
static constexpr uint8_t STICK_DOWN_RIGHT = 0x01, STICK_UP_LEFT = 0x02;
static constexpr uint8_t STICK_UP_RIGHT = 0x04, STICK_DOWN_LEFT = 0x08;

qbert::qbert()
  : qbert_main_rom(qbert_main_rom_blob), qbert_tiles(qbert_tiles_blob), qbert_sprites(qbert_sprites_blob),
    qbert_levels(qbert_levels_blob) {
  rom = qbert_main_rom.data();
  tiles = qbert_tiles.data();
  sprites = qbert_sprites.data();
  levels = qbert_levels.data();
  vblank = Arena::create<Seqlock<qbert_video::VideoState>>();
  video = Arena::create<qbert_video::VideoState>();
}

void qbert::reset() {
  machineBase::reset();
  ram = memory;
  memset(ram, 0xff, 0x1000);   // battery RAM 0000-0fff, MAME NVRAM default all 1
  const i8086::Bus bus = {this, busRead, busWrite, ram, QBERT_RAM_END, rom, ROM_BASE, 0xffff};
  i8086::setBus(bus);
  i8086::reset();
  booted = false;
  soundCmd = 0;
  outputLatch = 0;
}

// ---- 8088 I/O ----

uint8_t qbert::rdio(uint16_t a) {
  if (a >= IO_BASE) {
    switch (a & 7) {
      case IO_DSW:
        return QBERT_DSW | (input->demoSoundsOff() ? QBERT_DSW_DEMO_SOUND_OFF : 0);
      case IO_IN1:
        return readIn1(input->buttons_get());
      case IO_IN2:   // trackball, not fitted
      case IO_IN3:
        return 0xff;
      case IO_IN4:
        return readStick(input->buttons_get());
    }
    return 0;
  }
  if (a >= VRAM_BASE && a < CHAR_BASE) {
    return ram[QBERT_VRAM + (a & 0x3ff)];
  }
  return 0;   // sprite RAM and palette are write only, char RAM unused (ROM tiles)
}

void qbert::wrio(uint16_t a, uint8_t v) {
  if (a >= UNMAPPED) {
    return;   // ROM and empty sockets
  }
  if (a >= IO_BASE) {
    switch (a & 7) {
      case IO_SOUND:
        soundCmd = v;
        game_started = 1;
        break;
      case IO_OUTPUT:
        outputLatch = v;
        break;
    }
    return;   // watchdog, trackball reset: nothing to do
  }
  if (a >= PALETTE_BASE) {
    ram[QBERT_PALETTE + (a & 0x1f)] = v;
  } else if (a >= VRAM_BASE && a < CHAR_BASE) {
    ram[QBERT_VRAM + (a & 0x3ff)] = v;
  } else if (a < VRAM_BASE) {
    ram[QBERT_SPRITES + (a & 0xff)] = v;
  }
}

uint8_t qbert::readIn1(unsigned int keys) const {
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
uint8_t qbert::readStick(unsigned int keys) const {
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

// ---- Frame ----

void qbert::enterNmi(void) {
  i8086::nmi();
}

bool qbert::frameWait(void) const {
  const uint16_t ip = i8086::IP();
  if (ip != FRAME_WAIT && ip != FRAME_WAIT_SERVICE) {
    return false;
  }
  return i8086::AX() == (ram[FRAME_COUNTER] | ram[FRAME_COUNTER + 1] << 8);
}

// steps the 8088 until it waits for the next frame
qbert::CpuExit qbert::cpuRun(int32_t budget) {
  for (budgetLeft = budget; !frameWait(); budgetLeft--) {
    if (budgetLeft <= 0) {
      return CPU_BUDGET;
    }
    i8086::step();
    if (i8086::fault()) {
      return CPU_TRAP;
    }
  }
  return CPU_WAIT;
}

void qbert::runCpu(void) {
  const CpuExit e = cpuRun(BUDGET);
  if (e == CPU_BUDGET) {
    budgetCuts++;
  } else if (e == CPU_TRAP) {
    trapCount++;
    printf("qbert: 8088 trap at %04x:%04x, reset\n", i8086::CS(), i8086::IP());
    reset();
  }
}

void qbert::run_frame(void) {
  if (booted) {
    publishVideo();
    enterNmi();
  }
  booted = true;
  runCpu();
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

// at the vblank NMI, before its handler copies the next sprite list and
// palette in: what the board shows for the frame that just ended
void qbert::publishVideo(void) {
  vblank->publish([this](qbert_video::VideoState &v) {
    memcpy(v.vram, &ram[QBERT_VRAM], sizeof(v.vram));
    memcpy(v.spr, &ram[QBERT_SPRITES], sizeof(v.spr));
    memcpy(v.pal, &ram[QBERT_PALETTE], sizeof(v.pal));
    v.ctrl = outputLatch;
  });
}

void qbert::prepare_frame(void) {
  vblank->read(*video);
  qbert_video::buildPens(video->pal, levels, pens);
  spriteCount = qbert_video::listSprites(video->spr, spriteList);
}

void qbert::render_row(short row) {
  qbert_video::renderStrip(row, *video, pens, tiles, sprites, spriteList, spriteCount, frame_buffer);
}

#endif
