#include "tp84.h"
#include "tp84_rom_main.h"
#include "tp84_rom_sub.h"
#include "tp84_rom_audio.h"
#include "tp84_tiles.h"
#include "tp84_sprites.h"
#include "tp84_palette.h"

tp84::tp84()
  : tp84_rom_main(tp84_rom_main_blob), tp84_rom_sub(tp84_rom_sub_blob),
    tp84_rom_audio(tp84_rom_audio_blob), tp84_tiles(tp84_tiles_blob),
    tp84_sprites(tp84_sprites_blob), tp84_rgb(tp84_rgb_blob), tp84_lut(tp84_lut_blob) {
  // m6809_reset() reads the reset vectors through these
  main_rom = tp84_rom_main.data();
  sub_rom = tp84_rom_sub.data();
  audio_rom = tp84_rom_audio.data();
}

void tp84::reset() {
  machineBase::reset();               // clears memory[], mutes SN chips, resets Z80
  bands.reset();
  memset(&video, 0, sizeof(video));
  line = 0;
  main_irq_enable = sub_irq_mask = 0;
  palette_bank = scroll_x = scroll_y = 0;
  soundlatch = snd_irq_pend = 0;
  memset(sn_last_reg, 0, sizeof(sn_last_reg));
  snd_cycles = 0;

  m6809_reset(&main_cpu);
  main_cpu.rom_direct = main_rom;     // must follow m6809_reset()
  main_cpu.rom_base = 0x8000;
  main_cpu.rom_size = 0x8000;
  m6809_reset(&sub_cpu);
  sub_cpu.rom_direct = sub_rom;
  sub_cpu.rom_base = 0xE000;
  sub_cpu.rom_size = 0x2000;
}

void tp84::start() {
  reset();
  game_started = 1;
}

// Instruction fetches outside the ROM windows (RAM code)
unsigned char IRAM_ATTR tp84::m6809_read_opcode(m6809_state *s, uint16_t addr) {
  return m6809_read(s, addr);
}

unsigned char IRAM_ATTR tp84::m6809_read(m6809_state *s, uint16_t addr) {
  if (s == &sub_cpu) {
    if (addr >= 0xE000) return sub_rom[addr - 0xE000];
    if ((addr & 0xF800) == 0x8000) return memory[TP84_SHARED + (addr & 0x7FF)];
    if (addr >= 0x67A0 && addr < 0x6800) return bands.ram[addr - 0x67A0];
    if ((addr & 0xF800) == 0x6000) return memory[TP84_SUB_RAM + (addr & 0x7FF)];
    if (addr == 0x2000) return line;          // beam position
    return 0xFF;
  }
  if (addr >= 0x8000) return main_rom[addr - 0x8000];
  if (addr >= 0x4000 && addr < 0x5000) return memory[addr - 0x4000];
  if ((addr & 0xF800) == 0x5000) return memory[TP84_SHARED + (addr & 0x7FF)];
  return main_read_io(addr);
}

uint8_t tp84::main_read_io(uint16_t addr) {
  const unsigned int keys = input->buttons_get();
  uint8_t v = 0xFF;
  switch (addr) {
    case 0x2800:                                         // SYSTEM
      if (keys & BUTTON_COIN)  v &= ~0x01;
      if (keys & BUTTON_START) v &= ~0x08;
      return v;
    case 0x2820:                                         // P1, 8-way
      if (keys & BUTTON_LEFT)  v &= ~0x01;
      if (keys & BUTTON_RIGHT) v &= ~0x02;
      if (keys & BUTTON_UP)    v &= ~0x04;
      if (keys & BUTTON_DOWN)  v &= ~0x08;
      if (keys & (BUTTON_FIRE | BUTTON_A | BUTTON_L1)) v &= ~0x10;   // shoot
      if (keys & (BUTTON_START | BUTTON_B | BUTTON_R1)) v &= ~0x20;  // missile
      return v;
    case 0x2860: return TP84_DSW1;
    case 0x3000: {
      uint8_t d = TP84_DSW2;
      if (input->demoSoundsOff()) d |= TP84_DSW2_DEMO_SOUND_OFF;
      return d;
    }
  }
  return 0xFF;                                           // P2 0x2840, unmapped
}

void IRAM_ATTR tp84::m6809_write(m6809_state *s, uint16_t addr, uint8_t val) {
  if (s == &sub_cpu) {
    if ((addr & 0xF800) == 0x8000) { memory[TP84_SHARED + (addr & 0x7FF)] = val; return; }
    if (addr >= 0x67A0 && addr < 0x6800) { bands.write(addr - 0x67A0, val, line); return; }
    if ((addr & 0xF800) == 0x6000) { memory[TP84_SUB_RAM + (addr & 0x7FF)] = val; return; }
    if (addr == 0x4000) {
      sub_irq_mask = val & 1;
      if (!sub_irq_mask) sub_cpu.irq_pending = 0;
    }
    return;                                              // 0x0000 watchdog
  }
  if (addr >= 0x4000 && addr < 0x5000) { memory[addr - 0x4000] = val; return; }
  if ((addr & 0xF800) == 0x5000) { memory[TP84_SHARED + (addr & 0x7FF)] = val; return; }
  switch (addr) {
    case 0x2800: palette_bank = val; return;
    case 0x3000:                                         // LS259 Q0: irq enable
      main_irq_enable = val & 1;
      if (!main_irq_enable) main_cpu.irq_pending = 0;
      return;
    case 0x3800: snd_irq_pend = 1; return;
    case 0x3A00: soundlatch = val; return;
    case 0x3C00: scroll_x = val; return;
    case 0x3E00: scroll_y = val; return;
  }
  // 0x2000 watchdog, 0x3001-0x3007 coin counters / flip (upright): ignored
}

// ---- Sound Z80 ----

unsigned char tp84::opZ80(unsigned short Addr) { return rdZ80(Addr); }

unsigned char tp84::rdZ80(unsigned short Addr) {
  if (Addr < 0x2000) return audio_rom[Addr];
  if (Addr < 0x4000) return 0x00;                        // unpopulated ROM socket
  if ((Addr & 0xE000) == 0x4000) return memory[TP84_SND_RAM + (Addr & 0x3FF)];
  if ((Addr & 0xE000) == 0x6000) return soundlatch;
  if ((Addr & 0xE000) == 0x8000) return snd_cycles >> 10; // (cycles / 1024) & 0x0F
  return 0xFF;
}

void tp84::wrZ80(unsigned short Addr, unsigned char Value) {
  if ((Addr & 0xE000) == 0x4000) { memory[TP84_SND_RAM + (Addr & 0x3FF)] = Value; return; }
  switch (Addr) {
    case 0xC001: sn_write(0, Value); return;
    case 0xC003: sn_write(1, Value); return;
    case 0xC004: sn_write(2, Value); return;
  }
  // 0xA000-0xA1FF RC filters, 0xC000 nop
}

// SN76489 byte stream -> sn_period/sn_volume. Raw periods; the 1.79 MHz
// clock is handled by sn_inc in audio.cpp.
void tp84::sn_write(int chip, uint8_t data) {
  if (data & 0x80) {                 // latch: register + low 4 bits
    const int reg = (data >> 5) & 0x03;
    const int type = (data >> 4) & 0x01;   // 0 = frequency, 1 = volume
    sn_last_reg[chip] = (reg * 2) + type;

    if (type == 0) {
      if (reg == 3)
        sn_period[chip][3] = data & 0x07;
      else
        sn_period[chip][reg] = (sn_period[chip][reg] & 0x3F0) | (data & 0x0F);
    } else {
      const uint8_t vol = data & 0x0F;
      sn_volume[chip][reg] = vol;
      if (vol < sn_min_volume[chip][reg]) sn_min_volume[chip][reg] = vol;
      if (vol < 15) sn_hold[chip][reg] = 6;
    }
  } else {                           // data: high 6 bits of the frequency
    const int reg = sn_last_reg[chip] / 2;
    const int type = sn_last_reg[chip] % 2;
    if (type == 0) {
      if (reg == 3)
        sn_period[chip][3] = data & 0x07;
      else
        sn_period[chip][reg] = (sn_period[chip][reg] & 0x00F) | ((data & 0x3F) << 4);
    }
  }
}

// ---- Frame ----

// One frame = 256 beam lines from vblank (line 240) to the next vblank.
// Per line: both 6809s ~100 cycles, Z80 20 instructions. The sub CPU polls
// the beam (0x2000) to multiplex sprites, so the beam advances per line.
void tp84::run_frame(void) {
  if (main_irq_enable) m6809_irq(&main_cpu);
  if (sub_irq_mask) m6809_irq(&sub_cpu);
  bands.beginFrame();

  int main_debt = 0, sub_debt = 0;
  for (int n = 0; n < TP84_LINES; n++) {
    line = (uint8_t)(240 + n);                           // wraps 255 -> 0

    main_debt += TP84_CYCLES_LINE;
    while (main_debt > 0) {
      int c = m6809_step(&main_cpu, 4);
      main_debt -= (c > 0) ? c : 1;
    }
    sub_debt += TP84_CYCLES_LINE;
    while (sub_debt > 0) {
      int c = m6809_step(&sub_cpu, 4);
      sub_debt -= (c > 0) ? c : 1;
    }

    for (int i = 0; i < TP84_Z80_STEPS_LINE; i++) StepZ80(&cpu[0]);
    snd_cycles = (snd_cycles + TP84_Z80_CYCLES_LINE) & 0x3FFF;
    if (snd_irq_pend && (cpu[0].IFF & IFF_1)) {
      IntZ80(&cpu[0], INT_RST38);
      snd_irq_pend = 0;
    }
  }
  line = 240;

  publish_video();
}

// Task 4
void tp84::publish_video() { }
void tp84::prepare_frame(void) { }
void tp84::render_row(short row) { }
