#ifndef MHAVOC_H
#define MHAVOC_H

#include "../dvgMachine.h"
#include "../../emulation/pokey.h"
#include "../../emulation/spinner.h"
#include "../../emulation/avg.h"
#include "mhavoc_logo.h"
#include "mhavoc_dipswitches.h"
#include "mhavoc_layout.h"

// Atari Major Havoc, MAME mhavoc (rev 3), atari/mhavoc.cpp. Alpha M6502
// (2.5 MHz, game and AVG) and gamma M6502 (1.25 MHz, sound, inputs,
// EEPROM) talk through one latch per direction with received/transmitted
// flags; an alpha write NMIs gamma. AVG Major Havoc variant (paged vector
// ROM, 32 entry color RAM), four POKEYs. ROT0, panel layout in
// mhavoc_layout.h. Player 1 only.
//
// Controls: stick left/right turns the roller (emulation/spinner.h), FIRE
// (or A) jumps / fires, DOWN (or B) shield, START presses FIRE to start
// (the cabinet has no start buttons).
class mhavoc : public dvgMachine {
protected:
  // ROM assets, unpacked into the Arena on first data()
  Asset<unsigned char, COMPRESSED> mhavoc_rom;
  Asset<unsigned char, COMPRESSED> mhavoc_avgrom;
  Asset<unsigned char, COMPRESSED> mhavoc_gamma;
  Asset<unsigned char, COMPRESSED> mhavoc_vrom;
public:
  mhavoc();
  uint8_t machineType() override { return MCH_MHAVOC; }
  void reset() override;
  void run_frame() override;
  // MAME mixes each POKEY at 0.25, Black Widow's two at 0.5: half their sum
  void renderAudio(int16_t *out, int n) override {
    Pokey::renderBank(pokey, soundregs, out, n);
    audioShiftRight(out, n, 1);
    audioClampAll(out, n, AUDIO_MAX);
  }
  static const Logo &logo() { return mhavoc_logo; }
#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

private:
  static const uint8_t POKEYS = 4;
  // placement in dvgMachine::memory: alpha 0000-01ff and 0800-09ff below
  // VRAM_OFS, VRAM (4 KB), then the rest
  static const uint16_t RAM_HIGH_OFS = 0x0200;
  static const uint16_t BANK_OFS = VRAM_OFS + MAX_VRAM_SIZE, BANK_SIZE = 0x0600; // 2 banks
  static const uint16_t BETA_OFS = BANK_OFS + 2 * BANK_SIZE, BETA_SIZE = 0x0800;
  static const uint16_t COLOR_OFS = BETA_OFS + BETA_SIZE, COLOR_SIZE = 0x20;
  static const uint16_t GAMMA_RAM_OFS = COLOR_OFS + COLOR_SIZE, GAMMA_RAM_SIZE = 0x0800;
  static const uint16_t EEPROM_OFS = GAMMA_RAM_OFS + GAMMA_RAM_SIZE, EEPROM_SIZE = 0x0200;

  static uint8_t alpha_read(m6502_t *, uint16_t);
  static void alpha_write(m6502_t *, uint16_t, uint8_t);
  static uint8_t gamma_read(m6502_t *, uint16_t);
  static void gamma_write(m6502_t *, uint16_t, uint8_t);
  uint8_t *alpha_ram(uint16_t a);
  uint8_t in0() const;
  uint8_t in1() const;
  void out0(uint8_t v);
  void tick();
  void step_gamma(uint32_t alpha_cycles);
  bool gamma_idle() const;
  bool skip_alpha(uint32_t slice_end);
  uint32_t native_alpha();
  avg::Memory avg_memory() const;
  bool pokey_running(uint8_t n) const;
  uint8_t pokey_read(uint8_t n, uint8_t reg);
  void pokey_write(uint8_t n, uint8_t reg, uint8_t v);
  void control_input();

  uint16_t decode_frame(const uint8_t *vram) override;
  uint16_t map_lines(uint16_t count) override;
  const uint16_t *strip_palette(uint8_t strip) const override { return pal; }

  const unsigned char *rom = nullptr, *avgrom = nullptr, *gamma_rom = nullptr, *vrom = nullptr;

  m6502_t gamma;
  uint32_t gamma_cycles = 0; // gamma clock since reset
  int32_t gamma_debt = 0;    // alpha cycles gamma is behind
  bool gamma_held = false;   // out0 bit 3 low: gamma in reset

  // latch handshake, MAME names: alpha_* = alpha -> gamma direction
  uint8_t alpha_data = 0, gamma_data = 0;
  bool alpha_rcvd = false, alpha_xmtd = false, gamma_rcvd = false, gamma_xmtd = false;
  bool player_1 = false; // out0 bit 5: IN0 bits 6-7 read SERVICE, not COIN
  uint8_t out0_value = 0;
  uint8_t rom_page = 0, ram_page = 0;

  // 4.88 kHz LS161 IRQ counters, ticked every TICK alpha cycles
  uint32_t next_tick = 0;
  uint8_t alpha_irq_clock = 0, gamma_irq_clock = 0;
  bool alpha_irq_enable = true;

  avg::State avg_state; // emulation core only (decode_frame)
  uint32_t avg_done_at = 0; // cycles the AVG halts

  uint8_t allpot = 0;
  uint32_t random_base[POKEYS] = {}; // gamma cycle of the SKCTL reset release
  Pokey pokey[POKEYS] = {Pokey(Pokey::CLOCKS_1250KHZ), Pokey(Pokey::CLOCKS_1250KHZ),
                         Pokey(Pokey::CLOCKS_1250KHZ), Pokey(Pokey::CLOCKS_1250KHZ)};

  // quarter counts per frame: 4 counts at first, MAME's key delta (40) after
  // 18 frames
  static const uint8_t SPIN_MIN = 16, SPIN_MAX = 160, SPIN_ACCEL = 8;
  Spinner roller{SPIN_MIN, SPIN_MAX, SPIN_ACCEL};
  unsigned int keys = 0;        // buttons of this frame
  uint8_t start_timer = 0;      // frames since START, 0: idle
  bool start_held = false;

  uint16_t pal[mhavoc_layout::PALETTE_SIZE];
};

#endif
