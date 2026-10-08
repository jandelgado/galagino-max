#ifndef NAMCOIO_H
#define NAMCOIO_H

#include <stdint.h>
#include <string.h>

// Namco 56XX / 58XX custom I/O chips (MB8843 MCU), port of MAME
// namco/namcoio.cpp. The CPU writes a command to nibble 8 (args in 9-15),
// the chip runs it once per vblank and leaves results in nibbles 0-7.
//
//   CPU bus  <-> ram[16] (4 bit each) <-> customio run58()/run56() <-> ports
//
// The caller supplies the board wiring:
//   in(port)  -> active-low 4-bit input, port 0 = pins 38-41 (coins / DIP mux),
//                1 = pins 22-25, 2 = pins 26-29, 3 = pins 30-33 (buttons)
//   sel(v)    <- pin 13 output, drives the LS157 DIP multiplexer select
class NamcoIO {
public:
  void reset() {
    memset(ram, 0, sizeof(ram));
    lastcoins = lastbuttons = 0;
    credits = 0;
    for (int i = 0; i < 2; i++) {
      coins[i] = 0;
      coins_per_cred[i] = 1;
      creds_per_coin[i] = 1;
    }
  }

  uint8_t read(uint8_t off) const { return 0xF0 | ram[off & 0x0F]; }
  void write(uint8_t off, uint8_t v) { ram[off & 0x0F] = v & 0x0F; }

  template <typename In, typename Sel> void run58(In in, Sel sel) {
    switch (ram[8]) {
      case 1:   // read switch inputs
        read_ports(in, 4);
        break;
      case 2:
        set_coinage();
        break;
      case 3:   // credit mode, BCD credits in nibbles 2/3
        handle_coins(in, SWAP_58XX);
        break;
      case 4:
        read_dipmux(in, sel);
        break;
      case 5:
        bootcheck_lfsr();
        break;
    }
  }

  template <typename In, typename Sel> void run56(In in, Sel sel) {
    switch (ram[8]) {
      case 1:   // read switch inputs, 56XX writes nibbles 0-3
        read_ports(in, 0);
        sel(ram[9] & 1);
        break;
      case 2:
        set_coinage();
        break;
      case 4:   // credit mode, BCD credits in nibbles 0/1
        handle_coins(in, SWAP_56XX);
        break;
      case 8:
        bootcheck_sum();
        break;
      case 9:
        read_dipmux(in, sel);
        break;
    }
  }

private:
  static const uint8_t SWAP_56XX = 0;
  static const uint8_t SWAP_58XX = 2;

  uint8_t ram[16];
  int lastcoins, lastbuttons;
  int credits;
  int coins[2];
  int coins_per_cred[2];
  int creds_per_coin[2];

  template <typename In> void read_ports(In in, uint8_t base) {
    for (uint8_t p = 0; p < 4; p++) {
      ram[base + p] = ~in(p) & 0x0F;
    }
  }

  void set_coinage() {
    coins_per_cred[0] = ram[9];
    creds_per_coin[0] = ram[10];
    coins_per_cred[1] = ram[11];
    creds_per_coin[1] = ram[12];
  }

  // all four ports with pin 13 low (even nibbles), then high (odd nibbles)
  template <typename In, typename Sel> void read_dipmux(In in, Sel sel) {
    for (uint8_t s = 0; s < 2; s++) {
      sel(s);
      for (uint8_t p = 0; p < 4; p++) {
        ram[2 * p + s] = ~in(p) & 0x0F;
      }
    }
  }

  // credit_add changes only when this coin completes a credit
  void coin_inserted(uint8_t slot, int &credit_add) {
    coins[slot]++;
    if (coins[slot] >= (coins_per_cred[slot] & 7)) {
      credit_add = creds_per_coin[slot] - (coins_per_cred[slot] >> 3);
      coins[slot] -= coins_per_cred[slot] & 7;
    } else if (coins_per_cred[slot] & 8) {
      credit_add = 1;
    }
  }

  // the chip counts coins and starts itself, reports credits in BCD;
  // nibbles 4/6 = P1/P2 stick, 5/7 = buttons "level | edge"
  template <typename In> void handle_coins(In in, uint8_t swap) {
    int credit_add = 0, credit_sub = 0;

    int val = ~in(0);
    int toggled = val ^ lastcoins;
    lastcoins = val;

    if (val & toggled & 0x01) {
      coin_inserted(0, credit_add);
    }
    if (val & toggled & 0x02) {
      coin_inserted(1, credit_add);
    }
    if (val & toggled & 0x08) {   // service coin
      credit_add = 1;
    }

    val = ~in(3);
    toggled = val ^ lastbuttons;
    lastbuttons = val;

    // nibble 9 == 0: start buttons enabled
    if (ram[9] == 0) {
      if (val & toggled & 0x04) {
        if (credits >= 1) {
          credit_sub = 1;
        }
      } else if (val & toggled & 0x08) {
        if (credits >= 2) {
          credit_sub = 2;
        }
      }
    }

    credits += credit_add - credit_sub;

    ram[0 ^ swap] = (credits / 10) & 0x0F;
    ram[1 ^ swap] = (credits % 10) & 0x0F;
    if (credit_add) {
      ram[2 ^ swap] = credit_add & 0x0F;
    }
    if (credit_sub) {
      ram[3 ^ swap] = credit_sub & 0x0F;
    }

    ram[4] = ~in(1) & 0x0F;
    ram[5] = (((val & 0x05) << 1) | (val & toggled & 0x05)) & 0x0F;
    ram[6] = ~in(2) & 0x0F;
    ram[7] = ((val & 0x0A) | ((val & toggled & 0x0A) >> 1)) & 0x0F;
  }

  // 58XX boot check: repeated XORs of the args, driven by a 7-bit LFSR
  static int lfsr_next(int n) { return ((n & 1) ? n ^ 0x90 : n) >> 1; }

  void bootcheck_lfsr() {
    // arg order the LFSR walks through, one bit per arg
    static const uint8_t ARG[7] = { 11, 10, 9, 15, 14, 13, 12 };

    int seed = 0x22;
    for (int i = (ram[9] * 16 + ram[10]) & 0x7F; i > 0; i--) {
      seed = lfsr_next(seed);
    }

    for (int i = 1; i < 8; i++) {
      int n = 0;
      int rng = seed;
      for (int a = 0; a < 7; a++) {
        if (rng & 1) {
          n ^= ~ram[ARG[a]];
        }
        rng = lfsr_next(rng);
        if (a == 0) {
          seed = rng;
        }
      }
      ram[i] = ~n & 0x0F;
    }
    ram[0] = (ram[9] == 0x0F) ? 0x0F : 0x00;   // MAME kludge for gaplus
  }

  // 56XX boot check: nibbles 0-1 = sum of args
  void bootcheck_sum() {
    int sum = 0;
    for (int i = 9; i < 16; i++) {
      sum += ram[i];
    }
    ram[0] = (sum >> 4) & 0x0F;
    ram[1] = sum & 0x0F;
  }
};

#endif
