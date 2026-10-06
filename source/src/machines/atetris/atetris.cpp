#include "atetris.h"
#include "atetris_rom.h"
#include "atetris_tiles.h"
#include "../../emulation/pokey.h"

using namespace atetris_video;

// memory map (MAME atetris main_map), 16 bit bus
enum : uint16_t {
  RAM_END = 0x2000,       // work RAM + video RAM, identity in memory[]
  PALETTE_BASE = 0x2000,  // 0x2000-0x23ff, 256 entries mirrored
  EEPROM_BASE = 0x2400,   // 0x2400-0x25ff
  EEPROM_END = 0x2600,
  POKEY_BASE = 0x2800,    // 0x2800-0x2bff: bit 4 selects the chip, mirror 0x3e0
  WATCHDOG = 0x3000,      // 0x3000-0x33ff, ignored
  EEPROM_UNLOCK = 0x3400, // 0x3400-0x37ff
  IRQ_ACK = 0x3800,       // 0x3800-0x3bff
  COIN_COUNTER = 0x3c00,  // 0x3c00-0x3fff
  BANK_BASE = 0x4000,     // 0x4000-0x7fff slapstic bank
  ROM_FIXED = 0x8000,
  BLOCK_MASK = 0xfc00,
};

// POKEY register offsets beyond AUDF1..AUDC4 (0-7)
enum : uint8_t { POKEY_AUDCTL = 0x08, POKEY_ALLPOT = 0x08, POKEY_RANDOM = 0x0a, POKEY_SKCTL = 0x0f };
static const uint8_t AUDCTL_POLY9 = 0x80;
static const uint8_t SK_RESET = 0x03; // SKCTL bits, both clear = reset

// inputs, active high
enum : uint8_t {
  IN0_COIN1 = 0x02,
  IN0_NOT_VBLANK = 0x40, // MAME IP_ACTIVE_LOW vblank: 0 during vblank
  IN1_BUTTON = 0x01,     // start and rotate share one bit
  IN1_DOWN = 0x02,
  IN1_RIGHT = 0x04,
  IN1_LEFT = 0x08,
};
static const uint8_t COIN_COUNTER_BITS = 0x30;
static const uint8_t COIN_LED_FRAMES = 30;

// raster timing: IRQ line updated every 32 lines from line 16, vblank from 240
static const int IRQ_STEP = 16;
static const int VBLANK_LINE = 240;

atetris::atetris()
  : atetris_rom(atetris_rom_blob),
    atetris_tiles(atetris_tiles_blob) {
  memset(&m_cpu, 0, sizeof(m_cpu));
  m_cpu.read = main_read;
  m_cpu.write = main_write;
  m_cpu.user = this;
  rom = atetris_rom.data();
  tiles = atetris_tiles.data();
  buildSpans(FIT_CROP, crop_spans);
  buildSpans(FIT_SCALE, scale_spans);
}

void atetris::reset() {
  machineBase::reset();
  memset(memory + EEPROM_RAM, 0xff, EEPROM_SIZE); // blank 2804 (MAME nvram_default)
  frame_cycles = total_cycles = 0;
  memset(random_base, 0, sizeof(random_base));
  memset(allpot, 0, sizeof(allpot));
  in_vblank = eeprom_unlocked = false;
  coin_leds = 0;
  slapstic.reset();
  m6502_reset(&m_cpu);
}

// b6 is 0 during vblank (MAME IP_ACTIVE_LOW custom bit)
uint8_t atetris::in0() const {
  uint8_t v = ATETRIS_IN0_DIPS | (in_vblank ? 0 : IN0_NOT_VBLANK);
  if (input->buttons_get() & BUTTON_COIN) {
    v |= IN0_COIN1;
  }
  return v;
}

uint8_t atetris::in1() const {
  const uint8_t k = input->buttons_get();
  uint8_t v = 0;
  if (k & (BUTTON_FIRE | BUTTON_START)) {
    v |= IN1_BUTTON;
  }
  if (k & BUTTON_DOWN) {
    v |= IN1_DOWN;
  }
  if (k & BUTTON_RIGHT) {
    v |= IN1_RIGHT;
  }
  if (k & BUTTON_LEFT) {
    v |= IN1_LEFT;
  }
  return v;
}

bool atetris::pokey_running(int n) const {
  return soundregs[n * Pokey::REG_COUNT + Pokey::REG_SKCTL] & SK_RESET;
}

// ALLPOT (inputs) and RANDOM are the only POKEY reads, as millipede.cpp
uint8_t atetris::pokey_read(int n, uint8_t reg) {
  if (reg == POKEY_ALLPOT) {
    if (pokey_running(n)) {
      allpot[n] = n ? in1() : in0();
    }
    return allpot[n];
  }
  if (reg == POKEY_RANDOM) {
    const bool poly9 = soundregs[n * Pokey::REG_COUNT + Pokey::REG_AUDCTL] & AUDCTL_POLY9;
    const uint32_t clock = pokey_running(n) ? cpu_clock() - random_base[n] : 0;
    return Pokey::random(clock, poly9);
  }
  return 0;
}

void atetris::pokey_write(int n, uint8_t reg, uint8_t v) {
  uint8_t *regs = soundregs + n * Pokey::REG_COUNT;
  if (reg <= POKEY_AUDCTL) {
    regs[reg] = v;
  } else if (reg == POKEY_SKCTL) {
    if (!pokey_running(n) && (v & SK_RESET)) {
      random_base[n] = cpu_clock();
    }
    regs[Pokey::REG_SKCTL] = v;
  }
}

uint8_t atetris::read(uint16_t a) {
  if (a >= ROM_FIXED) {
    return rom[a];
  }
  if (a >= BANK_BASE) {
    return rom[(slapstic.bank() & 1) * 0x4000 + (a - BANK_BASE)];
  }
  if (a < RAM_END) {
    return memory[a];
  }
  switch (a & BLOCK_MASK) {
    case PALETTE_BASE: return memory[PALETTE_RAM + (a & 0xff)];
    case EEPROM_BASE:
      return a < EEPROM_END ? memory[EEPROM_RAM + (a & (EEPROM_SIZE - 1))] : 0;
    case POKEY_BASE: return pokey_read((a >> 4) & 1, a & 0x0f);
  }
  return 0;
}

void atetris::write(uint16_t a, uint8_t v) {
  if (a < RAM_END) {
    memory[a] = v;
    return;
  }
  switch (a & BLOCK_MASK) {
    case PALETTE_BASE:
      memory[PALETTE_RAM + (a & 0xff)] = v;
      return;
    case EEPROM_BASE:
      if (a < EEPROM_END && eeprom_unlocked) {
        memory[EEPROM_RAM + (a & (EEPROM_SIZE - 1))] = v;
        eeprom_unlocked = false; // lock_after_write
      }
      return;
    case POKEY_BASE:
      pokey_write((a >> 4) & 1, a & 0x0f, v);
      return;
    case EEPROM_UNLOCK:
      eeprom_unlocked = true;
      return;
    case IRQ_ACK:
      m_cpu.irq = 0;
      return;
    case COIN_COUNTER:
      if (v & COIN_COUNTER_BITS) {
        coin_leds = COIN_LED_FRAMES;
      }
      return;
  }
  // watchdog, ROM: ignored
}

// the slapstic sees every access after it happened (MAME read/write tap)
uint8_t atetris::main_read(m6502_t *cpu, uint16_t a) {
  atetris *s = static_cast<atetris *>(cpu->user);
  const uint8_t v = s->read(a);
  s->slapstic.access(a);
  return v;
}

void atetris::main_write(m6502_t *cpu, uint16_t a, uint8_t v) {
  atetris *s = static_cast<atetris *>(cpu->user);
  s->write(a, v);
  s->slapstic.access(a);
}

uint32_t atetris::cpu_clock() const {
  return total_cycles + frame_cycles;
}

void atetris::run_until(uint32_t cycle) {
  while (frame_cycles < cycle) {
    frame_cycles += m6502_step(&m_cpu);
  }
}

void atetris::run_frame() {
  for (int line = 0; line < LINES; line += IRQ_STEP) {
    in_vblank = line >= VBLANK_LINE;
    if (line == VBLANK_LINE) {
      vblank.publish([this](VideoState &v) {
        for (int ty = 0; ty < VIS_ROWS; ty++) {
          memcpy(v.vram[ty], memory + VIDEO_RAM + ty * RAM_COLS * CELL_BYTES, sizeof(v.vram[ty]));
        }
        memcpy(v.palette, memory + PALETTE_RAM, sizeof(v.palette));
      });
    }
    // MAME interrupt_gen: lines 16 + 32 n, asserted when line & 32
    if ((line & 31) == 16) {
      m_cpu.irq = (line & 32) ? 1 : 0;
    }

    const int end = line + IRQ_STEP < LINES ? line + IRQ_STEP : LINES;
    run_until((uint32_t)end * CYCLES_PER_FRAME / LINES);
  }
  frame_cycles -= CYCLES_PER_FRAME; // keep the overshoot
  total_cycles += CYCLES_PER_FRAME;
  if (!game_started) {
    game_started = 1;
  }
}

void atetris::prepare_frame() {
  vblank.read(video);
  for (int i = 0; i < 256; i++) {
    pal[i] = rgb332(video.palette[i]);
  }
  const bool crop = isGameLayout(video.vram[0]);
  spans = crop ? crop_spans : scale_spans;
  span_cols = crop ? OUT_WIDTH / 8 : VIS_COLS;
}

void atetris::render_row(short row) {
  // frame_buffer is already black outside the game area
  const int ty = row - FIRST_STRIP;
  if (ty < 0 || ty >= VIS_ROWS) {
    return;
  }
  const uint8_t *cells = video.vram[ty];
  for (int tx = 0; tx < span_cols; tx++) {
    const Span s = spans[tx];
    const uint8_t *cell = cells + tx * CELL_BYTES;
    const uint32_t *t = tiles[tileCode(cell)];
    const uint16_t *p = pal + tileColor(cell) * 16;
    for (int y = 0; y < 8; y++) {
      uint16_t *dst = frame_buffer + y * OUT_WIDTH + s.out;
      uint32_t w = t[y];
      if (s.keep == 0xff) {
        for (int i = 0; i < 8; i++, w >>= 4) {
          *dst++ = p[w & 15];
        }
      } else {
        for (int i = 0; i < 8; i++, w >>= 4) {
          if ((s.keep >> i) & 1) {
            *dst++ = p[w & 15];
          }
        }
      }
    }
  }
}

#ifdef LED_PIN
// placeholder colors until Task 6 derives them from the logo
void atetris::menuLeds(CRGB *leds) {
  fill_solid(leds, NUM_LEDS, CRGB::Black);
}

void atetris::gameLeds(CRGB *leds) {
  menuLeds(leds);
}
#endif
