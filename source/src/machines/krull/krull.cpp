#include "../../catalog.h"
#ifdef GAME_KRULL

#include "krull.h"
#include "krull_rom.h"
#include "krull_gfx.h"

// idle loop "inc ax; test byte [9E8h],0FFh; je $-6" until the NMI handler
// starts a logic frame (every 2nd NMI)
static constexpr uint16_t FRAME_WAIT = 0x1581, LOGIC_FLAG = 0x09e8;

// I/O 5800-5fff (mirror 0x07f8), gottlieb_base_map
static constexpr uint16_t IO_BASE = 0x5800;
static constexpr uint8_t IO_DSW = 0, IO_IN1 = 1, IO_IN2 = 2, IO_IN3 = 3, IO_IN4 = 4;
static constexpr uint8_t IO_SOUND = 2, IO_OUTPUT = 3;
static constexpr uint16_t VRAM_BASE = 0x3800, CHAR_BASE = 0x4000, PALETTE_BASE = 0x5000;

// tile code & 0x7f: MAME's 128 char RAM tiles wrap for codes 0x80-0xff
static constexpr uint8_t CODE_MASK = 0x7f;

// one video frame (318 x 256 pixels at 5 MHz) in sound CPU cycles
static constexpr uint32_t PIXEL_HZ = 5000000, FRAME_PIXELS = 318 * 256;

// IN1 (active high): service switch active low, kept open
static constexpr uint8_t IN1_SERVICE_OFF = 0x01, IN1_COIN1 = 0x04, IN1_START1 = 0x40;

// IN4: b0-3 right stick (throw), b4-7 left stick (move): up, right, down, left
static constexpr uint8_t IN4_FIRE_UP = 0x01, IN4_FIRE_RIGHT = 0x02, IN4_FIRE_DOWN = 0x04;
static constexpr uint8_t IN4_FIRE_LEFT = 0x08, IN4_MOVE_UP = 0x10, IN4_MOVE_RIGHT = 0x20;
static constexpr uint8_t IN4_MOVE_DOWN = 0x40, IN4_MOVE_LEFT = 0x80;

krull::krull()
  : krull_main_rom(krull_main_rom_blob), krull_sprites(krull_sprites_blob), krull_levels(krull_levels_blob),
    krull_sound_rom(krull_sound_rom_blob) {
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
  machineBase::reset();   // clears memory: video RAM, char RAM
  ram = memory;
  memset(ram, 0xff, KRULL_RAM_END);   // battery RAM, MAME NVRAM default all 1
  memset(charTiles, 0, CHAR_TILES * 8 * sizeof(uint32_t));
  const i8086::Bus bus = {this, busRead, busWrite, ram, KRULL_RAM_END, rom, CODE_BASE, ROM_BASE, 0xffff};
  i8086::setBus(bus);
  i8086::reset();
  booted = false;
  outputLatch = 0;
  stickState = 0;
  sticks = TwinStick();
  sound.reset();
  cmdCount = 0;
  soundPhase = 0;
  dc = DcBlock();
  lastSample = 0;
}

// ---- 8088 I/O ----

uint8_t krull::rdio(uint16_t a) {
  if (a >= IO_BASE) {
    switch (a & 7) {
      case IO_DSW:
        return KRULL_DSW | (input->demoSoundsOff() ? KRULL_DSW_DEMO_SOUND_OFF : 0);
      case IO_IN1:
        return readIn1(input->buttons_get());
      case IO_IN2:   // trackball, not fitted
      case IO_IN3:
        return 0xff;
      case IO_IN4:
        return readIn4();
    }
    return 0;
  }
  if (a >= CHAR_BASE && a < PALETTE_BASE) {
    return ram[KRULL_CHARRAM + (a & 0xfff)];
  }
  if (a >= VRAM_BASE && a < CHAR_BASE) {
    return ram[KRULL_VRAM + (a & 0x3ff)];
  }
  return 0;   // sprite RAM and palette are write only
}

void krull::wrio(uint16_t a, uint8_t v) {
  if (a < IO_START || a >= ROM_BASE) {
    return;   // ROM
  }
  if (a >= IO_BASE) {
    switch (a & 7) {
      case IO_SOUND:
        if (cmdCount < CMD_QUEUE) {
          cmdQueue[cmdCount++] = v;
        }
        break;
      case IO_OUTPUT:
        outputLatch = v;
        break;
    }
    return;   // watchdog, trackball reset: nothing to do
  }
  if (a >= PALETTE_BASE) {
    ram[KRULL_PALETTE + (a & 0x1f)] = v;
  } else if (a >= CHAR_BASE) {
    const uint16_t offset = a & 0xfff;
    ram[KRULL_CHARRAM + offset] = v;
    gottlieb_video::charWrite(charTiles, offset, v);
  } else if (a >= VRAM_BASE) {
    ram[KRULL_VRAM + (a & 0x3ff)] = v;
  } else {
    ram[KRULL_SPRITES + (a & 0xff)] = v;
  }
}

uint8_t krull::readIn1(unsigned int keys) const {
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
uint8_t krull::readIn4(void) const {
  const uint16_t s = stickState;
  return ((s & TwinStick::FIRE_UP) ? IN4_FIRE_UP : 0) | ((s & TwinStick::FIRE_RIGHT) ? IN4_FIRE_RIGHT : 0) |
         ((s & TwinStick::FIRE_DOWN) ? IN4_FIRE_DOWN : 0) | ((s & TwinStick::FIRE_LEFT) ? IN4_FIRE_LEFT : 0) |
         ((s & TwinStick::MOVE_UP) ? IN4_MOVE_UP : 0) | ((s & TwinStick::MOVE_RIGHT) ? IN4_MOVE_RIGHT : 0) |
         ((s & TwinStick::MOVE_DOWN) ? IN4_MOVE_DOWN : 0) | ((s & TwinStick::MOVE_LEFT) ? IN4_MOVE_LEFT : 0);
}

// ---- Frame ----

void krull::enterNmi(void) {
  i8086::nmi();
}

bool krull::frameWait(void) const {
  return i8086::IP() == FRAME_WAIT && ram[LOGIC_FLAG] == 0;
}

// steps the 8088 until it waits for the next frame
krull::CpuExit krull::cpuRun(int32_t budget) {
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

void krull::runCpu(void) {
  const CpuExit e = cpuRun(BUDGET);
  if (e == CPU_BUDGET) {
    budgetCuts++;
  } else if (e == CPU_TRAP) {
    trapCount++;
    printf("krull: 8088 trap at %04x:%04x, reset\n", i8086::CS(), i8086::IP());
    reset();
  }
}

void krull::run_frame(void) {
  stickState = sticks.update(input->buttons_get());
  if (booted) {
    publishVideo();
    enterNmi();
  }
  booted = true;
  game_started = 1;
  runCpu();
  runSound();
}

// ---- Sound ----

void krull::runSound(void) {
  const uint64_t ticks = (uint64_t)GottliebSound::SOUND_HZ * FRAME_PIXELS + soundPhase;
  soundPhase = (uint32_t)(ticks % PIXEL_HZ);
  sound.runFrame(cmdQueue, cmdCount, (uint32_t)(ticks / PIXEL_HZ));
  cmdCount = 0;
}

// audio core: drain the board's 24 kHz ring, hold the last value on underrun
void krull::renderAudio(int16_t *out, int n) {
  for (int i = 0; i < n; i++) {
    const int s = sound.sample();
    if (s != GottliebSound::EMPTY) {
      lastSample = (int16_t)s;
    }
    out[i] = audioClamp(dc.filter(lastSample), AUDIO_MAX);
  }
}

// ---- LEDs ----

#ifdef LED_PIN
// logo colors
static const CRGB KRULL_LED_GOLD(0xffb000), KRULL_LED_RED(0xc02000);

void krull::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[NUM_LEDS] = {
      KRULL_LED_RED, KRULL_LED_GOLD, KRULL_LED_GOLD, KRULL_LED_GOLD,
      KRULL_LED_GOLD, KRULL_LED_GOLD, KRULL_LED_RED};
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}
#endif

// ---- Video ----

// at the vblank NMI, before its handler copies the next sprite list and
// palette in: what the board shows for the frame that just ended
void krull::publishVideo(void) {
  vblank->publish([this](VideoState &v) {
    memcpy(v.vram, &ram[KRULL_VRAM], sizeof(v.vram));
    memcpy(v.tiles, charTiles, sizeof(v.tiles));
    memcpy(v.spr, &ram[KRULL_SPRITES], sizeof(v.spr));
    memcpy(v.pal, &ram[KRULL_PALETTE], sizeof(v.pal));
    v.ctrl = outputLatch;
  });
}

void krull::prepare_frame(void) {
  vblank->read(*video);
  gottlieb_video::buildPens(video->pal, levels, pens);
  spriteCount = gottlieb_video::listSprites(video->spr, spriteList);
}

void krull::render_row(short row) {
  gottlieb_video::renderStrip(row, video->vram, video->ctrl, pens, video->tiles, CODE_MASK, sprites,
                              spriteList, spriteCount, frame_buffer);
}

#endif
