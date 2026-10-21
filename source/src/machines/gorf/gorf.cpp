#include "gorf.h"
#include "gorf_rom.h"
#include "gorf_sc01.h"
#include "../../emulation/arena.h"
#include <new>

// Z80 opcode cycle table (static in Tables.h). The host test includes
// Z80.c, and with it Tables.h, into the same translation unit.
#ifndef GORF_Z80_TABLES_INCLUDED
#include "../../cpus/z80/Tables.h"
#endif

using namespace gorf_video;

// memory map (MAME wow_map)
enum : uint16_t {
  VRAM_BASE = 0x4000,    // 0x0000-0x3fff ROM, writes go through the funcgen
  ROM_HIGH = 0x8000,     // 0x8000-0xbfff, stored at rom[0x4000]
  ROM_EMPTY = 0xc000,    // 0xc000-0xcfff unpopulated ROM sockets
  RAM_BASE = 0xd000,     // 0xd000-0xd03f protected, 0xd040-0xdfff plain
  RAM_PLAIN = 0xd040,
  UNMAPPED = 0xe000,
};
static const uint16_t VRAM_BYTES = 0x4000, ROM_HIGH_OFFSET = 0x4000;
static const uint8_t EMPTY_ROM = 0xff;

// I/O ports (MAME port_map_stereo_pattern), low byte unless noted
enum : uint8_t {
  PORT_VIDEO_END = 0x10,   // 0x00-0x0f video registers
  PORT_SOUND0 = 0x10,      // 0x10-0x18 write: chip 0; 0x10-0x17 read: handles
  PORT_BLOCK = 0x18,       // sound block transfer, reg = port >> 8
  PORT_EXPAND = 0x19,
  PORT_POTS = 0x1c,        // 0x1c-0x1f
  PORT_SOUND_END = 0x20,
  PORT_SOUND1 = 0x50,      // 0x50-0x58 write: chip 1
  PORT_BLOCK1 = 0x58,
  PORT_PATTERN = 0x78,     // 0x78-0x7e
  PORT_PATTERN_END = 0x7f,
};
static const uint16_t PORT_RAM_UNLOCK = 0xa55b;  // full 16 bit address

// video registers (port & 0x0f)
enum : uint8_t {
  VR_MODE = 0x08,       // write: hires bit (Gorf always hires); read: intercept
  VR_SPLIT = 0x09,
  VR_VBLANK = 0x0a,
  VR_COLOR_BLOCK = 0x0b,
  VR_FUNCGEN = 0x0c,
  VR_IRQ_VECTOR = 0x0d,
  VR_IRQ_ENABLE = 0x0e, // read: vertical feedback
  VR_IRQ_LINE = 0x0f,   // read: horizontal feedback
};

// interrupt enable register bits
enum : uint8_t {
  IE_LIGHTPEN_PULSE = 0x01,  // 1: lightpen IRQ asserted for one instruction
  IE_LIGHTPEN = 0x12,        // either bit enables lightpen IRQs
  IE_LINE_PULSE = 0x04,      // 1: line IRQ asserted for one instruction
  IE_LINE = 0x08,
};

// sound chip 0 read strobes (so_cb), port & 7
enum : uint8_t { SO_OUTLATCH = 5, SO_VOTRAX = 7 };

// outlatch (CD4099) outputs
enum : uint8_t { Q_SPARKLE = 2, Q_SOUND_SWITCH = 6, Q_LAMP = 7 };

// input handles, active low
enum : uint8_t {
  H_COIN1 = 0x01, H_START1 = 0x10,
  H_UP = 0x01, H_DOWN = 0x02, H_LEFT = 0x04, H_RIGHT = 0x08, H_FIRE = 0x10,
  H_P3_UNUSED = 0x60, H_VOTRAX_REQUEST = 0x80,
};

// raster: 262 lines of 113.75 CPU cycles (1789773 Hz / 15734 Hz), Q8
static const uint16_t RASTER_LINES = 262;
static const int32_t CYCLES_PER_LINE_Q8 = 29120;
static const int32_t IRQ_ACK_CYCLES = 19;  // IM2 acknowledge
static const uint8_t OP_EI = 0xfb;
static const uint8_t LIGHTPEN_HFEEDBACK = 8;

// mix: chip level step 5 (0..300 unipolar per chip; the 30 Hz DC block
// passes onsets, so both chips from silence reach 600). Votrax (FULL_SCALE
// 16384, phrases peak ~16000) x 5 / 256 -> ~310, level with one chip; it
// only plays with chip 1 (q6 mutes chip 0): 610. Rare full-scale peaks clip
// at AUDIO_MAX; gain 4 kept them inside but sounded quieter than other games.
static const int CHIP_GAIN = 5;
static const int32_t SPEECH_GAIN = 5, SPEECH_SHIFT = 8;
static const uint32_t AUDIO_HZ = 24000;

gorf::gorf()
  : gorf_rom(gorf_rom_blob),
    gorf_sc01(gorf_sc01_blob) {
  rom = gorf_rom.data();

  // ctor runs once per machine session
  vram = Arena::alloc(VRAM_BYTES, 4);
  void *lock = Arena::alloc(sizeof(Seqlock<VideoState>), alignof(Seqlock<VideoState>));
  vblank = new (lock) Seqlock<VideoState>();
  video = Arena::alloc<VideoState>(1);
  memset(video, 0, sizeof(*video));

  // tables built once here: reset() runs before start() and uses speech
  speech.init(gorf_sc01.data(), Arena::alloc(VotraxSc01::TABLE_BYTES, 4));

  pal = Arena::alloc<uint16_t>(512);
  buildPalette(pal);
  rowSrc = Arena::alloc<uint16_t>(OUT_H);
  rowMerge = Arena::alloc<uint8_t>(OUT_H);
  buildRowMap(rowSrc, rowMerge);

  // star list sorted by game row; starStart[g] .. starStart[g + 1] - 1
  const uint16_t n = starCount();
  stars = Arena::alloc<uint32_t>(n);
  buildStars(stars);
  starStart = Arena::alloc<uint16_t>(GAME_ROWS + 1);
  uint16_t s = 0;
  for (int g = 0; g <= GAME_ROWS; g++) {
    while (s < n && (int)(stars[s] >> 8) < g) {
      s++;
    }
    starStart[g] = s;
  }
}

// both cores stopped
void gorf::reset() {
  machineBase::reset();  // clears memory[]: RAM 0xd000-0xdfff
  cpu[0].ICount = 0;
  cycleFrac = 0;
  memset(vram, 0, VRAM_BYTES);

  funcgen = astrocade::FuncGen();
  pattern = astrocade::Pattern();
  regs = Regs();
  irqEnable = irqVector = irqLine = pendingVector = 0;
  irqPending = irqPulse = eiShadow = false;
  vFeedback = hFeedback = 0;
  ramWriteEnable = false;
  soundSwitch = lamp = false;

  chip[0].reset();
  chip[1].reset();
  speech.reset();
  ring.reset(speech.sampleHz(), AUDIO_HZ);
  dc = DcBlock();
}

unsigned char GAME_IRAM gorf::opZ80(unsigned short Addr) {
  if (Addr < VRAM_BASE) {
    return rom[Addr];
  }
  if (Addr >= ROM_HIGH && Addr < ROM_EMPTY) {
    return rom[Addr - ROM_HIGH_OFFSET];
  }
  return rdZ80(Addr);
}

unsigned char GAME_IRAM gorf::rdZ80(unsigned short Addr) {
  if (Addr < VRAM_BASE) {
    return rom[Addr];
  }
  if (Addr < ROM_HIGH) {
    return vram[Addr - VRAM_BASE];
  }
  if (Addr < ROM_EMPTY) {
    return rom[Addr - ROM_HIGH_OFFSET];
  }
  if (Addr < RAM_BASE) {
    return EMPTY_ROM;
  }
  if (Addr < RAM_PLAIN) {
    // any access locks the protected RAM again
    ramWriteEnable = false;
    return memory[Addr - RAM_BASE];
  }
  if (Addr < UNMAPPED) {
    return memory[Addr - RAM_BASE];
  }
  return 0;
}

void GAME_IRAM gorf::wrZ80(unsigned short Addr, unsigned char Value) {
  if (Addr < VRAM_BASE) {
    // function generator: shift/rotate/expand, OR/XOR with the VRAM byte
    uint8_t *dst = &vram[Addr];
    uint8_t out;
    if (funcgen.apply(Value, *dst, &out)) {
      *dst = out;
    }
    return;
  }
  if (Addr < ROM_HIGH) {
    vram[Addr - VRAM_BASE] = Value;
    return;
  }
  if (Addr < RAM_BASE) {
    return;
  }
  if (Addr < RAM_PLAIN) {
    // stored only right after an OUT to PORT_RAM_UNLOCK
    if (ramWriteEnable) {
      memory[Addr - RAM_BASE] = Value;
    }
    ramWriteEnable = false;
    return;
  }
  if (Addr < UNMAPPED) {
    memory[Addr - RAM_BASE] = Value;
  }
}

// MAME input_mux_r: P1..P4HANDLE
uint8_t gorf::readHandle(uint8_t handle) {
  const uint16_t keys = input->buttons_get();
  switch (handle) {
  case 0:
    return GORF_P1_JUMPERS |
           (uint8_t)(~((keys & BUTTON_COIN ? H_COIN1 : 0) | (keys & BUTTON_START ? H_START1 : 0)) &
                     0x3f);
  case 1:
    return GORF_P2;
  case 2:
    return (speech.request() ? H_VOTRAX_REQUEST : 0) | H_P3_UNUSED |
           (uint8_t)(~((keys & BUTTON_UP ? H_UP : 0) | (keys & BUTTON_DOWN ? H_DOWN : 0) |
                       (keys & BUTTON_LEFT ? H_LEFT : 0) | (keys & BUTTON_RIGHT ? H_RIGHT : 0) |
                       (keys & BUTTON_FIRE ? H_FIRE : 0)) &
                     0x1f);
  default:
    return GORF_DSW | (input->demoSoundsOff() ? 0 : GORF_DSW_DEMO_SOUNDS_ON);
  }
}

// chip 0 read strobes: outlatch (sparkle, sound switch, lamp) and votrax
void gorf::soundOut(uint8_t fn, uint8_t data) {
  if (fn == SO_VOTRAX) {
    speech.write(data);
    return;
  }
  if (fn != SO_OUTLATCH) {
    return;
  }

  // CD4099 write_nibble_d0: q[(d >> 1) & 7] = d & 1
  const uint8_t q = (data >> 1) & 7;
  const bool bit = data & 1;
  if (q >= Q_SPARKLE && q < Q_SPARKLE + 4) {
    regs.sparkle[q - Q_SPARKLE] = bit;
  } else if (q == Q_SOUND_SWITCH) {
    soundSwitch = bit;
  } else if (q == Q_LAMP) {
    lamp = bit;
  }
}

unsigned char gorf::inZ80(unsigned short Port) {
  const uint8_t port = Port & 0xff;
  if (port < PORT_VIDEO_END) {
    switch (port) {
    case VR_MODE: {
      const uint8_t r = funcgen.intercept;
      funcgen.intercept = 0;
      return r;
    }
    case VR_IRQ_ENABLE:
      return vFeedback;
    case VR_IRQ_LINE:
      return hFeedback;
    }
    return 0xff;
  }
  if (port < PORT_BLOCK) {
    soundOut(port & 7, Port >> 8);
    return readHandle(port & 3);
  }
  if (port >= PORT_POTS && port < PORT_SOUND_END) {
    return 0;
  }
  return 0xff;
}

void gorf::outZ80(unsigned short Port, unsigned char Value) {
  if (Port == PORT_RAM_UNLOCK) {
    ramWriteEnable = true;
    return;
  }

  const uint8_t port = Port & 0xff;
  if (port < VR_MODE) {
    regs.colors[port] = Value;
    return;
  }
  if (port >= PORT_SOUND0 && port <= PORT_BLOCK) {
    chip[0].write(port == PORT_BLOCK ? Port >> 8 : port, Value);
    return;
  }
  if (port >= PORT_SOUND1 && port <= PORT_BLOCK1) {
    chip[1].write(port == PORT_BLOCK1 ? Port >> 8 : port, Value);
    return;
  }
  if (port >= PORT_PATTERN && port < PORT_PATTERN_END) {
    // the blit holds the bus: its cycles come off the CPU budget
    Bus bus = {this};
    cpu[0].ICount -= pattern.write(port - PORT_PATTERN, Value, bus);
    return;
  }
  if (port == PORT_EXPAND) {
    funcgen.setExpand(Value);
    return;
  }

  switch (port) {
  case VR_SPLIT:
    regs.split = 2 * (Value & 0x3f);
    regs.bg = ((Value >> 6) & 3) * 0x55;
    break;
  case VR_VBLANK:
    regs.vblank = Value;
    break;
  case VR_COLOR_BLOCK:
    regs.colors[(Port >> 8) & 7] = Value;
    break;
  case VR_FUNCGEN:
    funcgen.setControl(Value);
    break;
  case VR_IRQ_VECTOR:
    irqVector = Value;
    irqPending = false;
    break;
  case VR_IRQ_ENABLE:
    irqEnable = Value;
    irqPending = false;
    if (Value) {
      game_started = 1;
    }
    break;
  case VR_IRQ_LINE:
    irqLine = Value;
    irqPending = false;
    break;
  }
}

// MAME scanline_callback (astrocade line numbering): line IRQ, else the
// horizontal drive pulses the lightpen input on every line
void gorf::lineIrq(uint16_t line) {
  if (line == irqLine && (irqEnable & IE_LINE)) {
    irqPending = true;
    irqPulse = irqEnable & IE_LINE_PULSE;
    pendingVector = irqVector;
    return;
  }
  if (irqEnable & IE_LIGHTPEN) {
    irqPending = true;
    irqPulse = irqEnable & IE_LIGHTPEN_PULSE;
    pendingVector = irqVector & 0xf0;
    vFeedback = line;
    hFeedback = LIGHTPEN_HFEEDBACK;
  }
}

// one scanline of Z80 time. StepZ80 only counts prefixed opcodes, so the
// main opcode's cycles are charged here (Tables.h Cycles[]).
void gorf::runLine(void) {
  Z80 &z = cpu[0];
  cycleFrac += CYCLES_PER_LINE_Q8;
  z.ICount += cycleFrac >> 8;
  cycleFrac &= 0xff;

  while (z.ICount > 0) {
    // HOLD_LINE: pending until taken; pulse mode: one instruction only
    if (irqPending && !eiShadow && (z.IFF & IFF_1)) {
      irqPending = irqPulse = false;
      IntZ80(&z, pendingVector);
      z.ICount -= IRQ_ACK_CYCLES;
    }

    const uint8_t op = gorf::opZ80(z.PC.W);
    const int32_t left = z.ICount - Cycles[op];
    z.ICount = left;
    StepZ80(&z);

    // EI parks ICount for RunZ80; IRQs wait one more instruction
    eiShadow = op == OP_EI;
    if (eiShadow) {
      z.ICount = left;
    }
    if (irqPulse) {
      irqPending = irqPulse = false;
    }
  }
}

void gorf::run_frame(void) {
  for (uint16_t line = 0; line < RASTER_LINES; line++) {
    lineIrq(line);
    runLine();
    speech.line(ring);
  }

  // colors are written in vblank (lines 212-239), the bitmap during the
  // frame: latch both once the frame is complete
  vblank->publish([this](VideoState &v) {
    memcpy(v.vram, vram, sizeof(v.vram));
    v.regs = regs;
  });
}

void gorf::prepare_frame(void) {
  vblank->read(*video);
  frame++;
}

// 2 bit pixel of the render copy at (gameRow, column)
uint8_t GAME_IRAM gorf::pixelAt(int gameRow, int col) const {
  const Regs &r = video->regs;
  const int effy = col - TOP;
  const int lines = r.vblank < gorf_video::LINES ? r.vblank : gorf_video::LINES;
  const int p = GAME_ROWS - 1 - gameRow;
  const uint8_t d = (effy >= 0 && effy < lines) ? video->vram[effy * BYTES + (p >> 2)] : r.bg;
  return (d >> (6 - 2 * (p & 3))) & 3;
}

// stars on pixel 0 where the output stays black (both rows of a merged pair)
void GAME_IRAM gorf::drawStars(int gameRow, int gameRow2, uint16_t *dst) {
  const Regs &r = video->regs;
  const int last = gameRow2 >= 0 ? gameRow2 : gameRow;
  for (int g = gameRow; g <= last; g++) {
    const uint8_t *colors = &r.colors[((GAME_ROWS - 1 - g) >> 2) < r.split ? 4 : 0];
    for (uint16_t i = starStart[g]; i < starStart[g + 1]; i++) {
      const int col = stars[i] & 0xff;
      if (pixelAt(gameRow, col) || (gameRow2 >= 0 && pixelAt(gameRow2, col))) {
        continue;
      }
      dst[col] = sparkleColor(pal, colors[0], col, g, frame);
    }
  }
}

// 8 panel lines; each maps to one game row or a merged pair
void GAME_IRAM gorf::render_row(short row) {
  const Regs &r = video->regs;
  for (int l = 0; l < 8; l++) {
    const int y = row * 8 + l;
    const int g = rowSrc[y];
    const int g2 = rowMerge[y] ? g + 1 : -1;
    uint16_t *dst = frame_buffer + l * OUT_W;

    renderRow(video->vram, r, g, g2, pal, frame, dst);
    if (r.sparkle[0] == 0) {
      drawStars(g, g2, dst);
    }
  }
}

// q6 sound switch: 1 = votrax on, chip 0 muted; chip 1 always on.
// Chips are unipolar: DC removed before the bipolar votrax is added.
void gorf::renderAudio(int16_t *out, int n) {
  int32_t acc[AUDIO_CHUNK];
  while (n > 0) {
    const int k = n < AUDIO_CHUNK ? n : AUDIO_CHUNK;
    const bool voice = soundSwitch;
    memset(acc, 0, k * sizeof(acc[0]));
    chip[1].render(acc, k, CHIP_GAIN);
    if (!voice) {
      chip[0].render(acc, k, CHIP_GAIN);
    }

    // drained also while muted, else the ring holds stale speech
    ring.render(out, k);
    for (int i = 0; i < k; i++) {
      int32_t s = dc.filter(acc[i]);
      if (voice) {
        s += (out[i] * SPEECH_GAIN) >> SPEECH_SHIFT;
      }
      out[i] = audioClamp(s, AUDIO_MAX);
    }
    out += k;
    n -= k;
  }
}

const Logo &gorf::logo(void) {
  return gorf_logo;
}

#ifdef LED_PIN
// logo palette: GORF red, ship yellow; two mixes between them
static const CRGB GORF_LED_RED(0xff000a), GORF_LED_ORANGE(0xfb3d07);
static const CRGB GORF_LED_AMBER(0xf77b03), GORF_LED_YELLOW(0xf3b900);
static const uint8_t LAMP_BLINK_FRAMES = 8;

void gorf::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[NUM_LEDS] = {
      GORF_LED_RED, GORF_LED_ORANGE, GORF_LED_AMBER, GORF_LED_YELLOW,
      GORF_LED_AMBER, GORF_LED_ORANGE, GORF_LED_RED};
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}

// cabinet lamp (outlatch q7) lit: all LEDs blink red
void gorf::gameLeds(CRGB *leds) {
  menuLeds(leds);
  if (!lamp) {
    ledFrame = 0;
    return;
  }

  ledFrame = (ledFrame + 1) % (2 * LAMP_BLINK_FRAMES);
  if (ledFrame < LAMP_BLINK_FRAMES) {
    fill_solid(leds, NUM_LEDS, GORF_LED_RED);
  }
}
#endif
