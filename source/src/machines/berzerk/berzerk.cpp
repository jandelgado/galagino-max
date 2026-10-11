#include "berzerk.h"
#include "berzerk_rom.h"
#include "berzerk_speech.h"
#include "../../emulation/arena.h"
#include <new>

using namespace berzerk_video;

// memory map (MAME berzerk_map)
enum : uint16_t {
  NVRAM_BASE = 0x0800, // 0x0800-0x0bff, mirror 0x0c00-0x0fff
  ROM_HIGH = 0x1000,   // 0x1000-0x3fff
  VRAM_BASE = 0x4000,  // 0x4000-0x5fff read/write
  MAGIC_BASE = 0x6000, // 0x6000-0x7fff read VRAM, write through the ALU
  CRAM_BASE = 0x8000,  // 0x8000-0x87ff, mirrored up to 0xbfff
  UNMAPPED = 0xc000,
};
static const uint16_t NVRAM_MASK = 0x03ff, VRAM_MASK = 0x1fff, CRAM_MASK = 0x07ff;

// I/O ports (MAME berzerk_io_map, port & 0xff)
enum : uint8_t {
  PORT_AUDIO = 0x40,  // 0x40-0x47
  PORT_P1 = 0x48,
  PORT_SYSTEM = 0x49,
  PORT_P2 = 0x4a,
  PORT_MAGIC = 0x4b,
  PORT_NMI_ON = 0x4c,
  PORT_NMI_OFF = 0x4d,
  PORT_STATUS = 0x4e, // b7 !intercept, b0 V256
  PORT_IRQ_ON = 0x4f,
  PORT_DIPS = 0x60,   // 0x60-0x67, mirror 0x18
};
static const uint8_t AUDIO_PORTS = 8, DIP_PORTS_MASK = 0xe0;

// P1 / SYSTEM bits, active low
enum : uint8_t { P1_LEFT = 0x01, P1_RIGHT = 0x02, P1_UP = 0x04, P1_DOWN = 0x08, P1_FIRE = 0x10 };
enum : uint8_t { SYS_START1 = 0x01, SYS_COIN1 = 0x80 };

// raster: 262 lines, vpos 32..255 visible. ~10 Z80 cycles per instruction
// (machineBase INST_PER_FRAME): 160 cycles per line = 16 instructions.
static const uint16_t LINES = 262, VBLANK_START = 256;
static const int STEPS_PER_LINE = 4; // x 4 StepZ80
static const uint16_t IRQ_LINE_1 = 128, IRQ_LINE_2 = 256;
static const uint16_t NMI_PERIOD = 32, NMI_PHASE = 16; // vpos 16, 48, ... 240
static const uint16_t IRQ_VECTOR = 0xfc;

berzerk::berzerk()
  : berzerk_rom(berzerk_rom_blob),
    berzerk_speech(berzerk_speech_blob) {
  rom = berzerk_rom.data();

  // ctor runs once per machine session
  void *lock = Arena::alloc(sizeof(Seqlock<VideoState>), alignof(Seqlock<VideoState>));
  vblank = new (lock) Seqlock<VideoState>();
  video = Arena::alloc<VideoState>(1);
  memset(video, 0, sizeof(*video));

  buildColumnMap(column_map);
  for (int i = 0; i < 16; i++) {
    pens[i] = pen(i);
  }
}

// both cores stopped here
void berzerk::reset() {
  machineBase::reset(); // clears memory[]: NVRAM as MAME DEFAULT_ALL_0
  magic_ctrl = last_shift = intercept = 0;
  irq_enabled = irq_pending = nmi_enabled = false;
  vpos = 0;
  audio.reset(berzerk_speech.data());
}

unsigned char berzerk::opZ80(unsigned short Addr) {
  if (Addr < NVRAM_BASE || (Addr >= ROM_HIGH && Addr < VRAM_BASE)) {
    return rom[Addr];
  }
  return rdZ80(Addr);
}

unsigned char berzerk::rdZ80(unsigned short Addr) {
  if (Addr < NVRAM_BASE || (Addr >= ROM_HIGH && Addr < VRAM_BASE)) {
    return rom[Addr];
  }
  if (Addr < ROM_HIGH) {
    return memory[NVRAM + (Addr & NVRAM_MASK)];
  }
  if (Addr < CRAM_BASE) {
    return memory[VRAM + (Addr & VRAM_MASK)];
  }
  if (Addr < UNMAPPED) {
    return memory[CRAM + (Addr & CRAM_MASK)];
  }
  return 0;
}

// MAME magicram_w: shift, collision check, ALU against the current byte
uint8_t berzerk::magicWrite(uint8_t cur, uint8_t data) {
  const uint8_t s = shift(last_shift, data, magic_ctrl);
  if (s & cur) {
    intercept = 0;
  }
  last_shift = data & 0x7f;
  return alu(s, cur, magic_ctrl >> 4);
}

void berzerk::wrZ80(unsigned short Addr, unsigned char Value) {
  if (Addr >= NVRAM_BASE && Addr < ROM_HIGH) {
    memory[NVRAM + (Addr & NVRAM_MASK)] = Value;
    return;
  }
  if (Addr >= VRAM_BASE && Addr < MAGIC_BASE) {
    memory[VRAM + (Addr & VRAM_MASK)] = Value;
    return;
  }
  if (Addr >= MAGIC_BASE && Addr < CRAM_BASE) {
    uint8_t &cur = memory[VRAM + (Addr & VRAM_MASK)];
    cur = magicWrite(cur, Value);
    return;
  }
  if (Addr >= CRAM_BASE && Addr < UNMAPPED) {
    memory[CRAM + (Addr & CRAM_MASK)] = Value;
  }
}

uint8_t berzerk::readDip(uint8_t port) const {
  switch (port & 7) {
  case 0: return BERZERK_DIP_F3;
  case 1: return BERZERK_DIP_F2;
  case 2: return BERZERK_DIP_F6;
  case 3: return BERZERK_DIP_F5;
  case 4: return BERZERK_DIP_F4;
  case 5: return BERZERK_SW2;
  default: return 0; // LED off/on strobes
  }
}

unsigned char berzerk::inZ80(unsigned short Port) {
  const uint8_t port = Port & 0xff;
  if (port >= PORT_AUDIO && port < PORT_AUDIO + AUDIO_PORTS) {
    return audio.read(port - PORT_AUDIO);
  }
  if ((port & DIP_PORTS_MASK) == PORT_DIPS) {
    return readDip(port);
  }

  const uint16_t keys = input->buttons_get();
  switch (port) {
  case PORT_P1:
    return (uint8_t)~((keys & BUTTON_LEFT ? P1_LEFT : 0) | (keys & BUTTON_RIGHT ? P1_RIGHT : 0) |
                      (keys & BUTTON_UP ? P1_UP : 0) | (keys & BUTTON_DOWN ? P1_DOWN : 0) |
                      (keys & BUTTON_FIRE ? P1_FIRE : 0));
  case PORT_SYSTEM:
    return (uint8_t)~((keys & BUTTON_START ? SYS_START1 : 0) | (keys & BUTTON_COIN ? SYS_COIN1 : 0));
  case PORT_P2:
    return BERZERK_P2;
  case PORT_NMI_ON:
    nmi_enabled = true;
    return 0;
  case PORT_NMI_OFF:
    nmi_enabled = false;
    return 0;
  case PORT_STATUS:
    return (uint8_t)((intercept ^ 1) << 7 | (vpos < VIS_TOP || vpos >= VBLANK_START));
  }
  return 0;
}

void berzerk::outZ80(unsigned short Port, unsigned char Value) {
  const uint8_t port = Port & 0xff;
  if (port >= PORT_AUDIO && port < PORT_AUDIO + AUDIO_PORTS) {
    audio.write(port - PORT_AUDIO, Value);
    return;
  }

  switch (port) {
  case PORT_MAGIC:
    magic_ctrl = Value;
    last_shift = 0;
    intercept = 1;
    break;
  case PORT_NMI_ON:
    nmi_enabled = true;
    break;
  case PORT_NMI_OFF:
    nmi_enabled = false;
    break;
  case PORT_IRQ_ON:
    irq_enabled = Value & 1;
    if (irq_enabled) {
      game_started = 1;
    }
    break;
  }
}

// one raster frame, line by line: NMIs every 32 lines, two IRQs, sound
// stepped per line, video latched at vblank start
void berzerk::run_frame(void) {
  for (vpos = 0; vpos < LINES; vpos++) {
    if (vpos == VBLANK_START) {
      vblank->publish([this](VideoState &v) {
        memcpy(v.bitmap, &memory[VRAM + VIS_TOP * 32], sizeof(v.bitmap));
        memcpy(v.color, &memory[CRAM + VIS_TOP / 4 * 32], sizeof(v.color));
      });
    }
    if (nmi_enabled && vpos % NMI_PERIOD == NMI_PHASE) {
      IntZ80(&cpu[0], INT_NMI);
    }
    if (irq_enabled && (vpos == IRQ_LINE_1 || vpos == IRQ_LINE_2)) {
      irq_pending = true;
    }

    for (int i = 0; i < STEPS_PER_LINE; i++) {
      // MAME HOLD_LINE: stays pending until the CPU takes it
      if (irq_pending && (cpu[0].IFF & IFF_1)) {
        irq_pending = false;
        IntZ80(&cpu[0], (cpu[0].IFF & IFF_IM2) ? IRQ_VECTOR : INT_RST38);
      }
      StepZ80(&cpu[0]); StepZ80(&cpu[0]); StepZ80(&cpu[0]); StepZ80(&cpu[0]);
    }
    audio.line();
  }
}

void berzerk::prepare_frame(void) {
  vblank->read(*video);
}

// frame_buffer is cleared by the caller; only lit pixels are written
void berzerk::render_row(short row) {
  const int strip = row - FIRST_STRIP;
  if (strip < 0 || strip >= VIS_LINES / 8) {
    return;
  }

  for (int l = 0; l < 8; l++) {
    const int y = strip * 8 + l;
    const uint8_t *bits = video->bitmap[y];
    const uint8_t *colors = video->color[y >> 2];
    uint16_t *dst = frame_buffer + l * OUT_WIDTH;

    for (int b = 0; b < 32; b++) {
      uint8_t d = bits[b];
      if (!d) {
        continue;
      }

      // high nibble colors the left 4 px, low nibble the right 4 px
      const uint16_t left = pens[colors[b] >> 4], right = pens[colors[b] & 15];
      const int16_t *cols = column_map + b * 8;
      for (int i = 0; i < 8; i++, d <<= 1) {
        if ((d & 0x80) && cols[i] >= 0) {
          dst[cols[i]] = i < 4 ? left : right;
        }
      }
    }
  }
}

const Logo &berzerk::logo(void) {
  return berzerk_logo;
}

#ifdef LED_PIN
void berzerk::menuLeds(CRGB *leds) {
  defaultMenuLeds(leds);
}

void berzerk::gameLeds(CRGB *leds) {
  defaultMenuLeds(leds);
}
#endif
