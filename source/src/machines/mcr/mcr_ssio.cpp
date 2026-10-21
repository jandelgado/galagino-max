#include "../../catalog.h"
#if defined(GAME_TRON) || defined(GAME_DOTRON) || defined(GAME_SHOLLOW)

#include "mcr_ssio.h"
#include <string.h>

// Z80 opcode cycle table (static in Tables.h). The host test includes
// Z80.c, and with it Tables.h, into the same translation unit.
#ifndef MCR_Z80_TABLES_INCLUDED
#include "../../cpus/z80/Tables.h"
#endif

extern "C" unsigned char OpZ80_INL(unsigned short Addr);

static const uint8_t OP_EI = 0xfb;
static const int MIN_STEP_CYCLES = 4;

// ld a,(flag) / or a / jr z,$-4: 13 + 4 + 12 cycles per pass
static const uint8_t IDLE_LOOP[] = {0x3a, 0x00, 0x00, 0xb7, 0x28, 0xfa};
static const uint8_t IDLE_FLAG_LO = 1, IDLE_FLAG_HI = 2;
static const int IDLE_PASS_CYCLES = 29;
static const uint8_t RAM_PAGE = 0x8;  // 8000-8fff, see rd()

// ICount starts at 0 every step: block opcodes (LDIR) loop while ICount > 0,
// so a long-running count would turn them into one atomic copy.
int mcrStepZ80(Z80 &z) {
  const uint8_t op = OpZ80_INL(z.PC.W);
  z.ICount = -Cycles[op];
  StepZ80(&z);

  // EI parks ICount at 1 for RunZ80; undo that
  if (op == OP_EI) {
    z.ICount = -Cycles[op];
  }

  // HALT zeroes ICount and re-executes itself: still costs time
  const int used = -z.ICount;
  return used > MIN_STEP_CYCLES ? used : MIN_STEP_CYCLES;
}

void McrSsio::attach(const uint8_t *r, uint16_t size, uint8_t *m, const uint8_t *duty) {
  rom = r;
  romSize = size;
  ram = m;
  dutyGain = duty;
  findIdleLoop();
}

// The sound program waits for its ISR in a loop that only polls a RAM flag
// (tron 0x0101, dotron 0x010b, flag 0x8374). run() charges a pass of it
// without stepping the Z80: up to 3/4 of the sound CPU time.
void McrSsio::findIdleLoop() {
  idlePc = NO_IDLE_LOOP;
  for (uint16_t a = 0; a + sizeof(IDLE_LOOP) <= romSize; a++) {
    const uint8_t *p = rom + a;
    if (p[0] != IDLE_LOOP[0] || memcmp(p + 3, IDLE_LOOP + 3, sizeof(IDLE_LOOP) - 3)) {
      continue;
    }
    const uint16_t flag = p[IDLE_FLAG_LO] | (p[IDLE_FLAG_HI] << 8);
    if ((flag >> 12) != RAM_PAGE) {
      continue;
    }
    idlePc = a;
    idleFlag = flag & (RAM_SIZE - 1);
    return;
  }
}

void McrSsio::reset() {
  memset(latches, 0, sizeof(latches));
  memset(regs, 0, sizeof(regs));
  ayAddr[0] = ayAddr[1] = 0;
  statusByte = 0;
  count14024 = 0;
  countCycles = 0;
  debt = 0;
  irqLine = false;
}

// 7 bit 14024 counter at 50 kHz; /SINT follows its bit 6 (asserts at
// count 0x40, releases at the wrap to 0); a read of 0xe000 resets it.
void McrSsio::tickCounter(int cycles) {
  countCycles += cycles;
  while (countCycles >= CYCLES_PER_COUNT) {
    countCycles -= CYCLES_PER_COUNT;
    count14024 = (count14024 + 1) & 0x7f;
    if ((count14024 & 0x3f) == 0) {
      irqLine = (count14024 & 0x40) != 0;
    }
  }
}

void McrSsio::run(Z80 &cpu, int cycles) {
  debt += cycles;
  while (debt > 0) {
    if (irqLine && (cpu.IFF & IFF_1)) {
      IntZ80(&cpu, INT_RST38);
    }
    const bool idle = cpu.PC.W == idlePc && !ram[idleFlag];
    const int used = idle ? IDLE_PASS_CYCLES : mcrStepZ80(cpu);
    debt -= used;
    tickCounter(used);
  }
}

uint8_t McrSsio::rd(uint16_t a) {
  if (a < romSize) {
    return rom[a];
  }

  switch (a >> 12) {
  case 0x8:
    return ram[a & (RAM_SIZE - 1)];
  case 0x9:
    return latches[a & 3];
  case 0xa:
  case 0xb:
    if ((a & 3) == 1) {
      const int chip = (a >> 12) - 0xa;
      return regs[chip * Ay8910::REG_COUNT + ayAddr[chip]];
    }
    return 0xff;
  case 0xe:
    count14024 = 0;
    countCycles = 0;
    irqLine = false;
    return 0xff;
  default:
    return 0xff;  // unmapped, 0xf000 DIP switches all open
  }
}

void McrSsio::wr(uint16_t a, uint8_t v) {
  switch (a >> 12) {
  case 0x8:
    ram[a & (RAM_SIZE - 1)] = v;
    return;
  case 0xa:
  case 0xb: {
    const int chip = (a >> 12) - 0xa;
    if ((a & 3) == 0) {
      ayAddr[chip] = v & 0x0f;
    } else if ((a & 3) == 2) {
      regs[chip * Ay8910::REG_COUNT + ayAddr[chip]] = v;
    }
    return;
  }
  case 0xc:
    statusByte = v;
    return;
  default:
    return;
  }
}

// AY port A: duty of voices 0 (low nibble) and 1, port B: voice 2
void McrSsio::applyVolumes() {
  enum { PORT_A = 14, PORT_B = 15, MUTE = 0x80 };
  const bool mute = regs[Ay8910::REG_COUNT + PORT_B] & MUTE;

  for (int chip = 0; chip < 2; chip++) {
    const uint8_t *r = regs + chip * Ay8910::REG_COUNT;
    const uint8_t duty[3] = {(uint8_t)(r[PORT_A] & 0x0f), (uint8_t)(r[PORT_A] >> 4),
                             (uint8_t)(r[PORT_B] & 0x0f)};
    for (int v = 0; v < 3; v++) {
      ay[chip].setChannelGain(v, mute ? 0 : dutyGain[duty[v]]);
    }
  }
}

void McrSsio::render(int16_t *out, int n) {
  applyVolumes();
  Ay8910::renderBank(ay, regs, out, n);
  audioShiftRight(out, n, MIX_SHIFT);
}

#endif
