#ifdef ENABLE_CBDASH

#include "cbdash.h"
#include "cbdash_logo.h"
#include "cbdash_rom_main.h"
#include "cbdash_rom_sound.h"
#include "cbdash_rom_bios.h"
#include "cbdash_rom_audio.h"
#include "cbdash_sid.h"

// HLE boot: CPU state at the BIOS jump into the game, measured in MAME
static constexpr uint16_t MAIN_IMAGE_ADDR = 0x0500;
static constexpr uint16_t SOUND_IMAGE_ADDR = 0x0200;
static constexpr uint16_t MAIN_ENTRY = 0x0503;
static constexpr uint8_t MAIN_SP = 0xff;
static constexpr uint16_t SOUND_ENTRY = 0x0203;   // JMP $0223, sets its own SP
static constexpr uint16_t CHARRAM_ADDR = 0x6000;
static constexpr uint8_t BIOS_GAME_LOADED = 0xf8; // ram[0]: BIOS NMI (coin) handler enters the game
static constexpr uint8_t SOUND_GAME_LOADED = 0xff; // sound ram[0]: ROM IRQ handler passes commands to the game

static constexpr uint8_t DONGLE_ANSWER = 0x55;    // type 5: fixed value once latched
static constexpr uint8_t SOUND_CMD_MAIN_R = 0xc0; // e414 read, as MAME
static constexpr uint8_t SOUND_ACK_CMD = 0x80;    // command pending for the sound CPU
static constexpr uint8_t SOUND_ACK_DATA = 0x40;   // data pending for the main CPU
static constexpr uint8_t AY_REGS = 14;

// Sound commands (driver table at sound RAM $0881, MAME trace): 01 is the
// only music the game plays (the C64 melody, from coin to game start),
// cmd | 80 stops a sound (81 at game start), 00 is ignored by the driver.
static constexpr uint8_t SND_MUSIC = 0x01;
static constexpr uint8_t SND_STOP = 0x80;
static constexpr uint8_t SND_NONE = 0x00;
static constexpr uint8_t SID_SONG = 1;
// the tune peaks at 300 of Sid::SAMPLE_MAX (2 of 3 voices): x 2, 0.1%
// of the samples clip. Too quiet on the 8 bit DAC and speaker otherwise.
static constexpr int32_t MUSIC_GAIN = 2;

static constexpr uint8_t LED_FLASH_FRAMES = 8;

// Playfield redraw (PC 1bb4..1c41): writes the camera, then counts the
// cell rows left in ZP_DRAW_ROWS from 12 to 0, spread over ~1.5 frames.
//
// Smooth scroll. The game redraws the playfield once per tick (~11
// frames) and scrolls in 16 px cell steps. We glide instead:
//
// 1. main_write -> drawRows(): ZP_DRAW_ROWS going 0 -> nonzero starts a
//    redraw. Latch the camera, count draws.
// 2. publishVideo(): while a redraw is in progress, publish only the
//    frame counter, so a half drawn playfield never tears (max
//    MAX_PUBLISH_SKIPS in a row).
// 3. prepare_frame(): if the published camera moved, copy the render
//    copy to old (+2 KB) before reading the new one. old supplies the
//    band that scrolls out of the window.
// 4. scrollUpdate(): measure the tick period in emulated frames (display
//    frames jitter 5/6 at half rate). Glide only on a steady +-1 cell
//    step, snap on cave change, first draw or jumps.
// 5. scrollView(): offset runs 16 px -> 0 over one period, so the view
//    starts at the old position and trails the camera by up to one tick:
//
//      tick:     cam 9 ------------- cam 10 ------------- cam 11
//      display:  9 -- glide 16 px --> 10 -- glide 16 px --> 11
//
// 6. render_row(): playfield strips with a nonzero offset go through
//    renderShifted(). Each tile comes from the current view, else from
//    old shifted back one step, else background. HUD renders as usual.
//    ROT270: cam_x moves along output columns, cam_y along strips.
static constexpr uint16_t ZP_CAM_Y = 0x45;
static constexpr uint16_t ZP_CAM_X = 0x46;
static constexpr uint16_t ZP_DRAW_ROWS = 0x5a;
static constexpr uint8_t MAX_PUBLISH_SKIPS = 3; // never freeze if the game reuses ZP_DRAW_ROWS

// One frame = 17 slices of 16 scanlines (57.44 Hz, 272 lines), one sound
// NMI each. Cycles per frame as on real hardware: 750 kHz main = 13057,
// 500 kHz sound = 8705. Both CPUs interleaved, so back-to-back sound
// commands within a frame reach the sound CPU. VBLANK = last 32 lines.
static constexpr int SLICES = 17;
static constexpr int VBLANK_SLICES = 2;
static constexpr int MAIN_CYCLES_PER_SLICE = 768;
static constexpr int AUDIO_CYCLES_PER_SLICE = 512;

// DECO 222: bits 5 and 6 of every opcode fetch are swapped
static inline uint8_t deco222(uint8_t v) {
  return (v & 0x9f) | ((v & 0x20) << 1) | ((v & 0x40) >> 1);
}

// c800-cfff mirror fg/color RAM with row and column swapped
static inline uint16_t mirror(uint16_t offs) {
  return ((offs >> 5) & 0x1f) | ((offs & 0x1f) << 5);
}

cbdash::cbdash()
  : cbdash_rom_main(cbdash_rom_main_blob),
    cbdash_rom_sound(cbdash_rom_sound_blob),
    cbdash_rom_bios(cbdash_rom_bios_blob),
    cbdash_rom_audio(cbdash_rom_audio_blob),
    cbdash_sid(cbdash_sid_blob) {
  memset(&cpu_main, 0, sizeof(cpu_main));
  memset(&cpu_audio, 0, sizeof(cpu_audio));
  rom_bios = cbdash_rom_bios.data();
  rom_audio = cbdash_rom_audio.data();

  tune_ok = psidParse(cbdash_sid.data(), cbdash_sid.size(), &tune);
  if (tune_ok) {
    tune_ok = player.load(tune, Arena::alloc<uint8_t>(tune.payload_len), tune.payload_len);
  }
  if (!tune_ok) {
    printf("cbdash: cbdash_sid.h is not a supported PSID\n");
  }
}

void cbdash::start() {
  // start() runs twice; Arena has no free
  if (!main_ram) {
    main_ram = Arena::alloc(MAIN_RAM_SIZE);
  }
  boot();
}

// Inflates straight into RAM: the images are needed once per start, no
// Arena copy. decodeInto() may write one byte past the end, restore it.
static void unpack(const Asset<unsigned char, COMPRESSED> &image, uint8_t *dst) {
  uint8_t after = dst[image.size()];
  image.decodeInto(dst);
  dst[image.size()] = after;
}

// Replaces the tape load with the RAM and CPU state the BIOS leaves behind
void cbdash::boot() {
  memset(main_ram, 0, MAIN_RAM_SIZE);
  memset(memory, 0, MEM_END);
  unpack(cbdash_rom_main, main_ram + MAIN_IMAGE_ADDR);
  unpack(cbdash_rom_sound, memory + SOUND_RAM_OFFSET + SOUND_IMAGE_ADDR);
  main_ram[0x0000] = BIOS_GAME_LOADED;
  memory[SOUND_RAM_OFFSET + 0x0000] = SOUND_GAME_LOADED;

  memset(palette_ram, 0, sizeof(palette_ram));
  ccb = sound_latch = sound_latch2 = sound_ack = 0;
  ay_addr[0] = ay_addr[1] = 0;
  audio_nmi_enable = audio_reset = dongle_latch = coin_prev = false;
  led_flash = 0;
  publish_skips = 0;
  stopMusic();

  cpu_main.read = main_read;
  cpu_main.write = main_write;
  cpu_main.fetch = main_fetch;
  cpu_main.user = this;
  m6502_reset(&cpu_main);
  cpu_main.pc = MAIN_ENTRY;
  cpu_main.sp = MAIN_SP;

  cpu_audio.read = audio_read;
  cpu_audio.write = audio_write;
  cpu_audio.fetch = 0;
  cpu_audio.user = this;
  m6502_reset(&cpu_audio);
  cpu_audio.pc = SOUND_ENTRY;
}

// IN0, active high
static constexpr uint8_t IN0_RIGHT = 0x01;
static constexpr uint8_t IN0_LEFT = 0x02;
static constexpr uint8_t IN0_UP = 0x04;
static constexpr uint8_t IN0_DOWN = 0x08;
static constexpr uint8_t IN0_BUTTON1 = 0x10;
// IN2: start active high, coins active low
static constexpr uint8_t IN2_IDLE = 0xc0;
static constexpr uint8_t IN2_START1 = 0x08;
static constexpr uint8_t IN2_COIN1 = 0x80;

uint8_t cbdash::in0() {
  uint32_t keys = input->buttons_get();
  uint8_t v = 0;
  if (keys & BUTTON_RIGHT) { v |= IN0_RIGHT; }
  if (keys & BUTTON_LEFT)  { v |= IN0_LEFT; }
  if (keys & BUTTON_UP)    { v |= IN0_UP; }
  if (keys & BUTTON_DOWN)  { v |= IN0_DOWN; }
  if (keys & BUTTON_FIRE)  { v |= IN0_BUTTON1; }
  return v;
}

uint8_t cbdash::in2() {
  uint32_t keys = input->buttons_get();
  uint8_t v = IN2_IDLE;
  if (keys & BUTTON_START) { v |= IN2_START1; }
  if (keys & BUTTON_COIN)  { v &= ~IN2_COIN1; }
  return v;
}

// ---------------------------------------------------------------------------
// Main CPU
// ---------------------------------------------------------------------------
uint8_t cbdash::main_fetch(m6502_t *cpu, uint16_t addr) {
  return deco222(main_read(cpu, addr));
}

uint8_t cbdash::main_read(m6502_t *cpu, uint16_t addr) {
  cbdash *s = (cbdash *)cpu->user;

  if (addr < 0xc000) { return s->main_ram[addr]; }
  if (addr >= 0xf000) { return s->rom_bios[addr & 0x0fff]; }
  if (addr < 0xc400) { return s->memory[FG_RAM_OFFSET + (addr & 0x3ff)]; }
  if (addr < 0xc800) { return s->memory[COLOR_RAM_OFFSET + (addr & 0x3ff)]; }
  if (addr < 0xcc00) { return s->memory[FG_RAM_OFFSET + mirror(addr & 0x3ff)]; }
  if (addr < 0xd000) { return s->memory[COLOR_RAM_OFFSET + mirror(addr & 0x3ff)]; }
  if (addr < 0xdc00) { return s->memory[TILE_RAM_OFFSET + (addr - 0xd000)]; }
  if (addr >= 0xe000 && addr < 0xe100) { return s->palette_ram[addr & 0x1f]; }

  switch (addr) {
    case 0xe300: return CBDASH_DSW1 | (s->vblank ? 0x80 : 0x00);
    case 0xe301: return CBDASH_DSW2;
    case 0xe414: return SOUND_CMD_MAIN_R;
    case 0xe500: return s->dongle_latch ? DONGLE_ANSWER : 0xff;
    case 0xe600: return s->in0();
    case 0xe601: return 0x00; // cocktail player
    case 0xe602: return s->in2();
    case 0xe700: return s->sound_latch2;
    case 0xe701: return s->sound_ack;
  }
  return (addr >= 0xe603 && addr <= 0xe606) ? 0x00 : 0xff; // quadrature decoders
}

void cbdash::main_write(m6502_t *cpu, uint16_t addr, uint8_t val) {
  cbdash *s = (cbdash *)cpu->user;

  if (addr < 0xc000) {
    if (addr == ZP_DRAW_ROWS) {
      s->drawRows(val);
    }
    s->main_ram[addr] = val;
    return;
  }
  if (addr < 0xc400) { s->memory[FG_RAM_OFFSET + (addr & 0x3ff)] = val; return; }
  if (addr < 0xc800) { s->memory[COLOR_RAM_OFFSET + (addr & 0x3ff)] = val; return; }
  if (addr < 0xcc00) { s->memory[FG_RAM_OFFSET + mirror(addr & 0x3ff)] = val; return; }
  if (addr < 0xd000) { s->memory[COLOR_RAM_OFFSET + mirror(addr & 0x3ff)] = val; return; }
  if (addr < 0xdc00) { s->memory[TILE_RAM_OFFSET + (addr - 0xd000)] = val; return; }
  if (addr >= 0xe000 && addr < 0xe100) { s->palette_ram[addr & 0x1f] = val; return; }

  switch (addr) {
    case 0xe400: // b0: sound CPU reset, active high
      s->audio_reset = (val & 0x01) != 0;
      if (s->audio_reset) {
        s->stopMusic();
        s->audio_nmi_enable = false;
        m6502_reset(&s->cpu_audio);
      }
      return;

    case 0xe410:
      s->ccb = val;
      return;

    case 0xe414: // sound command: latch + IRQ to the sound CPU
      s->sound_latch = s->cueMusic(val) ? SND_NONE : val;
      s->sound_ack = (s->sound_ack | SOUND_ACK_CMD) & ~SOUND_ACK_DATA;
      s->cpu_audio.irq = 1;
      s->led_flash = LED_FLASH_FRAMES;
      return;

    case 0xe417: // main NMI clear
      s->cpu_main.nmi = 0;
      return;

    case 0xe501: // dongle type 5: 0xCx latches the fixed answer
      if ((val & 0xf0) == 0xc0) {
        s->dongle_latch = true;
      }
      return;
  }
  // watchdog, video mode/scroll/center registers, coin counter: unused by cbdash
}

// redraw start: latch the camera the playfield is drawn for
void cbdash::drawRows(uint8_t rows) {
  if (!rows || main_ram[ZP_DRAW_ROWS]) {
    return;
  }
  draw_cam_x = main_ram[ZP_CAM_X];
  draw_cam_y = main_ram[ZP_CAM_Y];
  draws++;
}

// next request for the audio core: sequence + 1, song
static uint16_t nextRequest(uint16_t request, uint8_t song) {
  return (uint16_t)(((request >> 8) + 1) << 8) | song;
}

// emulation core: music commands switch the SID tune, the sound CPU gets
// SND_NONE instead so the AY arrangement never starts
bool cbdash::cueMusic(uint8_t cmd) {
  if ((cmd & ~SND_STOP) != SND_MUSIC) {
    return false;
  }
  music_request = nextRequest(music_request, (cmd & SND_STOP) ? 0 : SID_SONG);
  return true;
}

// emulation core: boot and sound CPU reset silence the music, as on the AY
void cbdash::stopMusic() {
  music_request = nextRequest(music_request, 0);
}

// audio core: tune starts run the 6502 init here
void cbdash::renderSid(int16_t *out, int n) {
  const uint16_t request = music_request;
  if (tune_ok && request != music_seen) {
    music_seen = request;
    const uint8_t song = request & 0xff;
    sid_on = song != 0;
    if (sid_on) {
      player.start(song);
    }
  }

  if (!sid_on) {
    memset(out, 0, n * sizeof(*out));
    return;
  }

  player.render(out, n);
  for (int i = 0; i < n; i++) {
    const int32_t v = out[i] * MUSIC_GAIN;
    out[i] = (int16_t)(v > Sid::SAMPLE_MAX ? Sid::SAMPLE_MAX : (v < -Sid::SAMPLE_MAX ? -Sid::SAMPLE_MAX : v));
  }
}

// ---------------------------------------------------------------------------
// Sound CPU (6502, plain) + 2x AY-3-8910
// ---------------------------------------------------------------------------
uint8_t cbdash::audio_read(m6502_t *cpu, uint16_t addr) {
  cbdash *s = (cbdash *)cpu->user;

  if (addr < 0x1000) { return s->memory[SOUND_RAM_OFFSET + addr]; }
  if (addr >= 0xf800) { return s->rom_audio[addr & 0x07ff]; }

  if (addr < 0x1800) { // reading enables the NMI too
    s->audio_nmi_enable = true;
    return 0xff;
  }
  if (addr < 0x2000) {
    s->sound_ack &= ~SOUND_ACK_DATA;
    return 0xff;
  }
  if (addr >= 0xa000 && addr < 0xb000) {
    cpu->irq = 0;
    s->sound_ack &= ~SOUND_ACK_CMD;
    return s->sound_latch;
  }
  return 0xff;
}

void cbdash::audio_write(m6502_t *cpu, uint16_t addr, uint8_t val) {
  cbdash *s = (cbdash *)cpu->user;

  if (addr < 0x1000) { s->memory[SOUND_RAM_OFFSET + addr] = val; return; }
  if (addr < 0x1800) { s->audio_nmi_enable = true; return; }
  if (addr < 0x2000) { s->sound_ack &= ~SOUND_ACK_DATA; return; }

  if (addr >= 0x2000 && addr < 0x3000) {
    if (s->ay_addr[0] < AY_REGS) { s->soundregs[0x00 + s->ay_addr[0]] = val; }
    return;
  }
  if (addr >= 0x4000 && addr < 0x5000) { s->ay_addr[0] = val & 0x0f; return; }
  if (addr >= 0x6000 && addr < 0x7000) {
    if (s->ay_addr[1] < AY_REGS) { s->soundregs[0x10 + s->ay_addr[1]] = val; }
    return;
  }
  if (addr >= 0x8000 && addr < 0x9000) { s->ay_addr[1] = val & 0x0f; return; }

  if (addr >= 0xc000 && addr < 0xd000) {
    s->sound_latch2 = val;
    s->sound_ack |= SOUND_ACK_DATA;
  }
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------
void cbdash::run_frame(void) {
  // coin raises NMI, the game clears it via e417
  bool coin_now = (input->buttons_get() & BUTTON_COIN) != 0;
  if (coin_now && !coin_prev) {
    cpu_main.nmi = 1;
  }
  coin_prev = coin_now;

  // the game busy-waits on DSW1 b7, so it must see VBLANK once per frame
  vblank = false;
  for (int i = 0; i < SLICES; i++) {
    if (i == SLICES - VBLANK_SLICES) {
      publishVideo();
      vblank = true;
    }
    m6502_exec(&cpu_main, MAIN_CYCLES_PER_SLICE);

    if (audio_reset) {
      continue;
    }
    m6502_exec(&cpu_audio, AUDIO_CYCLES_PER_SLICE);
    if (audio_nmi_enable) {
      cpu_audio.nmi = 1;
    }
  }

  if (led_flash) {
    led_flash--;
  }
  if (!game_started) {
    game_started = 1;
  }
}

// latch render input at vblank start, as the hardware scans it out
void cbdash::publishVideo() {
  // a half drawn playfield tears: keep the last complete one
  frame++;
  if (main_ram[ZP_DRAW_ROWS] && publish_skips < MAX_PUBLISH_SKIPS) {
    publish_skips++;
    vblank_state.publish([this](cbdash_video::VideoState &v) {
      v.frame = frame;
    });
    return;
  }
  publish_skips = 0;

  vblank_state.publish([this](cbdash_video::VideoState &v) {
    v.frame = frame;
    if (v.draws != draws) {
      v.draw_frame = frame;
    }
    memcpy(v.fg, memory + FG_RAM_OFFSET, sizeof(v.fg));
    memcpy(v.color, memory + COLOR_RAM_OFFSET, sizeof(v.color));
    memcpy(v.palette, palette_ram, sizeof(v.palette));
    v.ccb = ccb;
    v.cam_x = draw_cam_x;
    v.cam_y = draw_cam_y;
    v.draws = draws;
  });
}

void cbdash::prepare_frame(void) {
  using namespace cbdash_video;

  // camera moves: keep the current view as the glide source
  bool moved = false;
  vblank_state.visit([&](const VideoState &v) {
    moved = v.cam_x != video.cam_x || v.cam_y != video.cam_y;
  });
  if (moved) {
    memcpy(&old, &video, sizeof(old));
  }

  vblank_state.read(video);
  scrollUpdate(scroll, video, moved);
  view = scrollView(scroll);

  // pen p shows palette RAM entry p ^ 16 (inverted A4)
  for (int p = 0; p < PENS; p++) {
    pens[p] = color565(video.palette[p ^ 16]);
  }
  sprite_count = collectSprites(video, sprites);
}

void cbdash::render_row(short row) {
  using namespace cbdash_video;
  if ((view.ox || view.oy) && row >= PF_FIRST_STRIP && row <= PF_LAST_STRIP) {
    renderShifted(frame_buffer, row, main_ram + CHARRAM_ADDR, video, old, view, pens);
    return;
  }
  cbdash_video::renderStrip(frame_buffer, row, main_ram + CHARRAM_ADDR, video, pens, sprites, sprite_count);
}

const Logo &cbdash::logo(void) {
  return cbdash_logo;
}

#ifdef LED_PIN
static const CRGB CBDASH_LED_RED = CRGB(0xe60000);
static const CRGB CBDASH_LED_GOLD = CRGB(0xffc828);
static const CRGB CBDASH_LED_DARK = CRGB(0x6e0000);

void cbdash::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[NUM_LEDS] = {
      CBDASH_LED_DARK, CBDASH_LED_RED, CBDASH_LED_GOLD, CBDASH_LED_GOLD,
      CBDASH_LED_GOLD, CBDASH_LED_RED, CBDASH_LED_DARK};
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}

// gold flash for a few frames after each sound command (diamonds, rocks)
void cbdash::gameLeds(CRGB *leds) {
  if (!led_flash) {
    menuLeds(leds);
    return;
  }
  fill_solid(leds, NUM_LEDS, CBDASH_LED_GOLD);
}
#endif

#endif // ENABLE_CBDASH
