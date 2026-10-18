#include "mb88xx.h"

// Port of MAME mb88xx.cpp, see mb88xx.h. Flag macros of the original:
// st = status (branch taken when 1), set to 1 by most instructions.

static const uint8_t TIMER_PRESCALE = 32; // MAME: guess

static const uint8_t INT_CAUSE_TIMER = 0x02;
static const uint8_t INT_CAUSE_EXTERNAL = 0x04;

static const uint8_t PIO_INTERNAL_CLOCK = 0x80;

// st = 0 on carry out of bit 3
static inline uint8_t st_c(uint8_t v) { return (v & 0x10) ? 0 : 1; }
static inline uint8_t st_z(uint8_t v) { return v == 0 ? 0 : 1; }
static inline uint8_t cf_of(uint8_t v) { return (v & 0x10) ? 1 : 0; }
static inline uint8_t zf_of(uint8_t v) { return v == 0 ? 1 : 0; }

void Mb88::reset() {
  PC = PA = 0;
  SP[0] = SP[1] = SP[2] = SP[3] = 0;
  SI = 0;
  A = X = Y = 0;
  st = 1;
  zf = cf = vf = sf = 0;
  pio = 0;
  TH = TL = TP = 0;
  SB = 0;
  pending_irq = 0;
  in_irq = false;
  icount = 0;
}

void Mb88::set_irq(bool state) {
  const uint8_t s = state ? 1 : 0;
  if (!irq_line && s && (pio & INT_CAUSE_EXTERNAL)) {
    pending_irq |= INT_CAUSE_EXTERNAL;
  }
  irq_line = s;
}

void Mb88::run(int cycles) {
  icount += cycles;
  while (icount > 0) {
    step();
  }
}

void Mb88::inc_pc() {
  PC++;
  if (PC >= 0x40) {
    PC = 0;
    PA++;
  }
}

// 8-bit PLA: CF selects the O nibble (0 = O3-O0, 1 = O7-O4)
void Mb88::write_pla(uint8_t index) {
  const uint8_t shift = (index & 0x10) ? 4 : 0;
  const uint8_t mask = 0x0f << shift;
  o_output = (o_output & ~mask) | ((index << shift) & mask);
  write_o(o_output, mask);
}

void Mb88::increment_timer() {
  TL = (TL + 1) & 0x0f;
  if (TL != 0) {
    return;
  }
  TH = (TH + 1) & 0x0f;
  if (TH == 0) {
    vf = 1;
    pending_irq |= INT_CAUSE_TIMER;
  }
}

void Mb88::burn_cycles(int cycles) {
  icount -= cycles;

  if (pio & PIO_INTERNAL_CLOCK) {
    TP += cycles;
    while (TP >= TIMER_PRESCALE) {
      TP -= TIMER_PRESCALE;
      increment_timer();
    }
  }

  if (in_irq || !(pending_irq & pio)) {
    return;
  }

  // push return address and flags, vector to 0x02 (external) or 0x04 (timer)
  in_irq = true;
  SP[SI] = ((PA << 6) + PC) | (cf << 15) | (zf << 14) | (st << 13);
  SI = (SI + 1) & 3;
  PC = (pending_irq & pio & INT_CAUSE_EXTERNAL) ? 0x02 : 0x04;
  PA = 0;
  st = 1;
  pending_irq = 0;
  burn_cycles(3);
}

void Mb88::step() {
  const uint8_t opcode = fetch();
  inc_pc();
  int oc = 1;
  uint8_t arg;

  switch (opcode) {
    case 0x00: // nop
      st = 1;
      break;
    case 0x01: // outO
      write_pla((cf << 4) | A);
      st = 1;
      break;
    case 0x02: // outP
      write_p(A);
      st = 1;
      break;
    case 0x03: // outR
      write_r(Y & 3, A);
      st = 1;
      break;
    case 0x04: // tay
      Y = A;
      st = 1;
      break;
    case 0x05: // tath
      TH = A;
      st = 1;
      break;
    case 0x06: // tatl
      TL = A;
      st = 1;
      break;
    case 0x07: // tas
      SB = A;
      st = 1;
      break;
    case 0x08: // icy
      Y++;
      st = st_c(Y);
      Y &= 0x0f;
      zf = zf_of(Y);
      break;
    case 0x09: // icm
      arg = rdmem(ea()) + 1;
      st = st_c(arg);
      arg &= 0x0f;
      zf = zf_of(arg);
      wrmem(ea(), arg);
      break;
    case 0x0a: // stic
      wrmem(ea(), A);
      Y++;
      st = st_c(Y);
      Y &= 0x0f;
      zf = zf_of(Y);
      break;
    case 0x0b: // x
      arg = rdmem(ea());
      wrmem(ea(), A);
      A = arg;
      zf = zf_of(A);
      st = 1;
      break;
    case 0x0c: // rol
      A = (A << 1) | cf;
      st = st_c(A);
      cf = st ^ 1;
      A &= 0x0f;
      zf = zf_of(A);
      break;
    case 0x0d: // l
      A = rdmem(ea());
      zf = zf_of(A);
      st = 1;
      break;
    case 0x0e: // adc
      arg = rdmem(ea()) + A + cf;
      st = st_c(arg);
      cf = st ^ 1;
      A = arg & 0x0f;
      zf = zf_of(A);
      break;
    case 0x0f: // and
      A &= rdmem(ea());
      zf = zf_of(A);
      st = zf ^ 1;
      break;
    case 0x10: // daa
      if (cf || A > 9) {
        A += 6;
      }
      st = st_c(A);
      cf = st ^ 1;
      A &= 0x0f;
      break;
    case 0x11: // das
      if (cf || A > 9) {
        A += 10;
      }
      st = st_c(A);
      cf = st ^ 1;
      A &= 0x0f;
      break;
    case 0x12: // inK
      A = read_k() & 0x0f;
      zf = zf_of(A);
      st = 1;
      break;
    case 0x13: // inR
      A = read_r(Y & 3) & 0x0f;
      zf = zf_of(A);
      st = 1;
      break;
    case 0x14: // tya
      A = Y;
      zf = zf_of(A);
      st = 1;
      break;
    case 0x15: // ttha
      A = TH;
      zf = zf_of(A);
      st = 1;
      break;
    case 0x16: // ttla
      A = TL;
      zf = zf_of(A);
      st = 1;
      break;
    case 0x17: // tsa
      A = SB;
      zf = zf_of(A);
      st = 1;
      break;
    case 0x18: // dcy
      Y--;
      st = st_c(Y);
      Y &= 0x0f;
      break;
    case 0x19: // dcm
      arg = rdmem(ea()) - 1;
      st = st_c(arg);
      arg &= 0x0f;
      zf = zf_of(arg);
      wrmem(ea(), arg);
      break;
    case 0x1a: // stdc
      wrmem(ea(), A);
      Y--;
      st = st_c(Y);
      Y &= 0x0f;
      zf = zf_of(Y);
      break;
    case 0x1b: // xx
      arg = X;
      X = A;
      A = arg;
      zf = zf_of(A);
      st = 1;
      break;
    case 0x1c: // ror
      A |= cf << 4;
      st = st_c(A << 4);
      cf = st ^ 1;
      A = (A >> 1) & 0x0f;
      zf = zf_of(A);
      break;
    case 0x1d: // st
      wrmem(ea(), A);
      st = 1;
      break;
    case 0x1e: // sbc
      arg = rdmem(ea()) - A - cf;
      st = st_c(arg);
      cf = st ^ 1;
      A = arg & 0x0f;
      zf = zf_of(A);
      break;
    case 0x1f: // or
      A |= rdmem(ea());
      zf = zf_of(A);
      st = zf ^ 1;
      break;
    case 0x20: // setR
      arg = read_r(Y >> 2) & 0x0f;
      write_r(Y >> 2, arg | (1 << (Y & 3)));
      st = 1;
      break;
    case 0x21: // setc
      cf = 1;
      st = 1;
      break;
    case 0x22: // rstR
      arg = read_r(Y >> 2) & 0x0f;
      write_r(Y >> 2, arg & ~(1 << (Y & 3)));
      st = 1;
      break;
    case 0x23: // rstc
      cf = 0;
      st = 1;
      break;
    case 0x24: // tstr
      arg = read_r(Y >> 2) & 0x0f;
      st = (arg & (1 << (Y & 3))) ? 0 : 1;
      break;
    case 0x25: // tsti
      st = irq_line ^ 1;
      break;
    case 0x26: // tstv
      st = vf ^ 1;
      vf = 0;
      break;
    case 0x27: // tsts
      st = sf ^ 1;
      sf = 0;
      break;
    case 0x28: // tstc
      st = cf ^ 1;
      break;
    case 0x29: // tstz
      st = zf ^ 1;
      break;
    case 0x2a: // sts
      wrmem(ea(), SB);
      zf = zf_of(SB);
      st = 1;
      break;
    case 0x2b: // ls
      SB = rdmem(ea());
      zf = zf_of(SB);
      st = 1;
      break;
    case 0x2c: // rts
      SI = (SI - 1) & 3;
      PC = SP[SI] & 0x3f;
      PA = (SP[SI] >> 6) & 0x1f;
      st = 1;
      break;
    case 0x2d: // neg
      A = (~A + 1) & 0x0f;
      st = st_z(A);
      break;
    case 0x2e: // c
      arg = rdmem(ea()) - A;
      cf = cf_of(arg);
      arg &= 0x0f;
      st = st_z(arg);
      zf = st ^ 1;
      break;
    case 0x2f: // eor
      A ^= rdmem(ea());
      st = st_z(A);
      zf = st ^ 1;
      break;
    case 0x30: case 0x31: case 0x32: case 0x33: // sbit
      wrmem(ea(), rdmem(ea()) | (1 << (opcode & 3)));
      st = 1;
      break;
    case 0x34: case 0x35: case 0x36: case 0x37: // rbit
      wrmem(ea(), rdmem(ea()) & ~(1 << (opcode & 3)));
      st = 1;
      break;
    case 0x38: case 0x39: case 0x3a: case 0x3b: // tbit
      st = (rdmem(ea()) & (1 << (opcode & 3))) ? 0 : 1;
      break;
    case 0x3c: // rti, restores the flags saved on interrupt entry
      in_irq = false;
      SI = (SI - 1) & 3;
      PC = SP[SI] & 0x3f;
      PA = (SP[SI] >> 6) & 0x1f;
      st = (SP[SI] >> 13) & 1;
      zf = (SP[SI] >> 14) & 1;
      cf = (SP[SI] >> 15) & 1;
      break;
    case 0x3d: // jpa imm
      PA = fetch() & 0x1f;
      PC = A * 4;
      oc++;
      st = 1;
      break;
    case 0x3e: // en imm
      pio |= fetch();
      inc_pc();
      oc++;
      st = 1;
      break;
    case 0x3f: // dis imm
      pio &= ~fetch();
      inc_pc();
      oc++;
      st = 1;
      break;
    case 0x40: case 0x41: case 0x42: case 0x43: // setD
      write_r(0, (read_r(0) & 0x0f) | (1 << (opcode & 3)));
      st = 1;
      break;
    case 0x44: case 0x45: case 0x46: case 0x47: // rstD
      write_r(0, (read_r(0) & 0x0f) & ~(1 << (opcode & 3)));
      st = 1;
      break;
    case 0x48: case 0x49: case 0x4a: case 0x4b: // tstD
      st = (read_r(2) & (1 << (opcode & 3))) ? 0 : 1;
      break;
    case 0x4c: case 0x4d: case 0x4e: case 0x4f: // tba
      st = (A & (1 << (opcode & 3))) ? 0 : 1;
      break;
    case 0x50: case 0x51: case 0x52: case 0x53: // xd
      arg = rdmem(opcode & 3);
      wrmem(opcode & 3, A);
      A = arg;
      zf = zf_of(A);
      st = 1;
      break;
    case 0x54: case 0x55: case 0x56: case 0x57: // xyd
      arg = rdmem((opcode & 3) + 4);
      wrmem((opcode & 3) + 4, Y);
      Y = arg;
      zf = zf_of(Y);
      st = 1;
      break;
    case 0x58: case 0x59: case 0x5a: case 0x5b:
    case 0x5c: case 0x5d: case 0x5e: case 0x5f: // lxi
      X = opcode & 7;
      zf = zf_of(X);
      st = 1;
      break;
    case 0x60: case 0x61: case 0x62: case 0x63:
    case 0x64: case 0x65: case 0x66: case 0x67: // call imm
      arg = fetch();
      inc_pc();
      oc++;
      if (st) {
        SP[SI] = (PA << 6) + PC;
        SI = (SI + 1) & 3;
        PC = arg & 0x3f;
        PA = ((opcode & 7) << 2) | (arg >> 6);
      }
      st = 1;
      break;
    case 0x68: case 0x69: case 0x6a: case 0x6b:
    case 0x6c: case 0x6d: case 0x6e: case 0x6f: // jpl imm
      arg = fetch();
      inc_pc();
      oc++;
      if (st) {
        PC = arg & 0x3f;
        PA = ((opcode & 7) << 2) | (arg >> 6);
      }
      st = 1;
      break;
    case 0x70: case 0x71: case 0x72: case 0x73:
    case 0x74: case 0x75: case 0x76: case 0x77:
    case 0x78: case 0x79: case 0x7a: case 0x7b:
    case 0x7c: case 0x7d: case 0x7e: case 0x7f: // ai
      arg = (opcode & 0x0f) + A;
      st = st_c(arg);
      cf = st ^ 1;
      A = arg & 0x0f;
      zf = zf_of(A);
      break;
    case 0x80: case 0x81: case 0x82: case 0x83:
    case 0x84: case 0x85: case 0x86: case 0x87:
    case 0x88: case 0x89: case 0x8a: case 0x8b:
    case 0x8c: case 0x8d: case 0x8e: case 0x8f: // lyi
      Y = opcode & 0x0f;
      zf = zf_of(Y);
      st = 1;
      break;
    case 0x90: case 0x91: case 0x92: case 0x93:
    case 0x94: case 0x95: case 0x96: case 0x97:
    case 0x98: case 0x99: case 0x9a: case 0x9b:
    case 0x9c: case 0x9d: case 0x9e: case 0x9f: // li
      A = opcode & 0x0f;
      zf = zf_of(A);
      st = 1;
      break;
    case 0xa0: case 0xa1: case 0xa2: case 0xa3:
    case 0xa4: case 0xa5: case 0xa6: case 0xa7:
    case 0xa8: case 0xa9: case 0xaa: case 0xab:
    case 0xac: case 0xad: case 0xae: case 0xaf: // cyi
      arg = (opcode & 0x0f) - Y;
      cf = cf_of(arg);
      arg &= 0x0f;
      st = st_z(arg);
      zf = st ^ 1;
      break;
    case 0xb0: case 0xb1: case 0xb2: case 0xb3:
    case 0xb4: case 0xb5: case 0xb6: case 0xb7:
    case 0xb8: case 0xb9: case 0xba: case 0xbb:
    case 0xbc: case 0xbd: case 0xbe: case 0xbf: // ci
      arg = (opcode & 0x0f) - A;
      cf = cf_of(arg);
      arg &= 0x0f;
      st = st_z(arg);
      zf = st ^ 1;
      break;
    default: // jmp within the page
      if (st) {
        PC = opcode & 0x3f;
      }
      st = 1;
      break;
  }

  burn_cycles(oc);
}
