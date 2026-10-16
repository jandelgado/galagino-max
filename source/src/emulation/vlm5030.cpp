#include "vlm5030.h"
#include <string.h>

// MAME tms5110r.hxx vlm5030_coeff
static const uint8_t PITCH_BITS = 5, ENERGY_BITS = 5;

static const uint8_t ENERGY[32] = {0,  1,  2,  3,  5,  6,  7,  9,  11, 13, 15,
                                   17, 19, 22, 24, 27, 31, 34, 38, 42, 47, 51,
                                   57, 62, 68, 75, 82, 89, 98, 107, 116, 127};

static const uint8_t PITCH[32] = {0,  21, 22, 23, 24, 25, 26, 27, 28, 29, 31,
                                  33, 35, 37, 39, 41, 43, 45, 49, 53, 57, 61,
                                  65, 69, 73, 77, 85, 93, 101, 109, 117, 125};

static const int16_t K1[64] = {
    390,  403,  414,  425,  434,  443,  450,  457,  463,  469,  474,  478, 482,
    485,  488,  491,  494,  496,  498,  499,  501,  502,  503,  504,  505, 506,
    507,  507,  508,  508,  509,  509,  -390, -376, -360, -344, -325, -305, -284,
    -261, -237, -211, -183, -155, -125, -95,  -64,  -32,  0,    32,   64,  95,
    125,  155,  183,  211,  237,  261,  284,  305,  325,  344,  360,  376};

// entry 16: MAME notes a special function or a manufacturing error
static const int16_t K2[32] = {0,    50,   100,  149,  196,  241,  284,  325,
                               362,  396,  426,  452,  473,  490,  502,  510,
                               0,    -510, -502, -490, -473, -452, -426, -396,
                               -362, -325, -284, -241, -196, -149, -100, -50};

static const int16_t K3_4[16] = {0,    64,   128,  192,  256,  320,  384, 448,
                                 -512, -448, -384, -320, -256, -192, -128, -64};

static const int16_t K5_10[8] = {0, 128, 256, 384, -512, -384, -256, -128};

static const int16_t *const KTABLE[10] = {K1,    K2,    K3_4,  K3_4,  K5_10,
                                          K5_10, K5_10, K5_10, K5_10, K5_10};

// frame bit layout: K1 index at bit 42 (6 bits) ... K10 at bit 11
static const uint8_t KBITS[10] = {6, 5, 4, 4, 3, 3, 3, 3, 3, 3};
static const uint8_t KPOS[10] = {42, 37, 33, 29, 26, 23, 20, 17, 14, 11};

static const uint8_t FRAME_BYTES = 6;

// interpolation steps per frame; samples per step by speed parameter
static const int16_t FR_SIZE = 4;
static const uint8_t SPEED[8] = {160 / FR_SIZE, 120 / FR_SIZE, 80 / FR_SIZE, 80 / FR_SIZE,
                                 160 / FR_SIZE, 240 / FR_SIZE, 200 / FR_SIZE, 200 / FR_SIZE};

// command byte: bit 0 extended frame, bit 1 end of speech
enum : uint8_t { CMD_EXTENDED = 0x01, CMD_END = 0x02 };

static const int32_t K_ONE = 512;    // K and the output are 10 bit signed
static const int SAMPLE_SHIFT = 6;   // 10 bit output to int16

void Vlm5030::reset(const uint8_t *r, uint16_t size) {
  rom = r;
  rom_mask = size - 1;
  pin_st = pin_rst = false;
  latch = 0;
  chip_reset();
  phase = IDLE;
}

// MAME device_reset
void Vlm5030::chip_reset() {
  phase = IDLE;
  address = 0;
  pin_bsy = false;

  old_energy = old_pitch = 0;
  new_energy = new_pitch = 0;
  current_energy = current_pitch = 0;
  target_energy = target_pitch = 0;
  memset(old_k, 0, sizeof(old_k));
  memset(new_k, 0, sizeof(new_k));
  memset(current_k, 0, sizeof(current_k));
  memset(target_k, 0, sizeof(target_k));
  interp_count = sample_count = pitch_count = 0;
  memset(x, 0, sizeof(x));
  setup_parameter(0);
}

// parameter byte: bits 0-1 bit rate (interpolation step), 3-5 speed, 6-7
// low / high pitch
void Vlm5030::setup_parameter(uint8_t param) {
  if (param & 2) {
    interp_step = 4;
  } else if (param & 1) {
    interp_step = 2;
  } else {
    interp_step = 1;
  }

  frame_size = SPEED[(param >> 3) & 7];

  if (param & 0x80) {
    pitch_offset = -8;
  } else if (param & 0x40) {
    pitch_offset = 8;
  } else {
    pitch_offset = 0;
  }
}

// RST: falling edge latches the parameters, rising edge resets a busy chip
void Vlm5030::rst_w(bool level) {
  if (pin_rst == level) {
    return;
  }
  pin_rst = level;

  if (!level) {
    setup_parameter(latch);
    return;
  }
  if (pin_bsy) {
    chip_reset();
  }
}

// ST: rising edge sets BSY, falling edge starts the phrase from the table
void Vlm5030::st_w(bool level) {
  if (pin_st == level) {
    return;
  }
  pin_st = level;

  if (level) {
    pin_bsy = true;
    return;
  }

  const uint16_t table = (latch & 0xfe) + ((latch & 1) << 8);
  address = (read_byte(table) << 8) | read_byte(table + 1);

  // one frame of the previous parameters before the first parse
  sample_count = frame_size;
  interp_count = FR_SIZE;
  phase = RUN;
}

int Vlm5030::get_bits(int sbit, int bits) const {
  const uint16_t offset = address + (sbit >> 3);
  int data = read_byte(offset) | (read_byte(offset + 1) << 8);
  data >>= (sbit & 7);
  return data & (0xff >> (8 - bits));
}

// next frame into new_*; returns its length in interpolation steps, 0 at
// the end of speech
int Vlm5030::parse_frame() {
  old_energy = new_energy;
  old_pitch = new_pitch;
  memcpy(old_k, new_k, sizeof(old_k));

  const uint8_t cmd = read_byte(address);
  if (cmd & CMD_EXTENDED) {
    new_energy = new_pitch = 0;
    memset(new_k, 0, sizeof(new_k));
    address++;
    if (cmd & CMD_END) {
      return 0;
    }
    // silent frames: MAME uses all bits above the command, not just SS
    return ((cmd >> 2) + 1) * 2 * FR_SIZE;
  }

  new_pitch = PITCH[get_bits(1, PITCH_BITS)];
  if (new_pitch > 0) {
    new_pitch += pitch_offset;
  }
  new_energy = ENERGY[get_bits(6, ENERGY_BITS)];
  for (int i = 0; i < NUM_K; i++) {
    new_k[i] = KTABLE[i][get_bits(KPOS[i], KBITS[i])];
  }

  address += FRAME_BYTES;
  return FR_SIZE;
}

// frame boundary: the previous frame becomes the interpolation start
void Vlm5030::load_frame() {
  interp_count = parse_frame();
  if (interp_count == 0) {
    interp_count = FR_SIZE;
    sample_count = frame_size;
    phase = STOP;
  }

  current_energy = old_energy;
  current_pitch = old_pitch;
  memcpy(current_k, old_k, sizeof(current_k));

  // a silent start frame stays silent instead of fading in
  if (current_energy == 0) {
    target_energy = 0;
    target_pitch = current_pitch;
    memcpy(target_k, current_k, sizeof(target_k));
    return;
  }
  target_energy = new_energy;
  target_pitch = new_pitch;
  memcpy(target_k, new_k, sizeof(target_k));
}

// interpolation step: current at 25, 50, 75, 100 % from old to target
void Vlm5030::next_step() {
  sample_count = frame_size;
  if (interp_count == 0) {
    load_frame();
  }

  interp_count -= interp_step;
  const int32_t effect = FR_SIZE - (interp_count % FR_SIZE);
  current_energy = old_energy + (target_energy - old_energy) * effect / FR_SIZE;
  if (old_pitch > 1) {
    current_pitch = old_pitch + (target_pitch - old_pitch) * effect / FR_SIZE;
  }
  for (int i = 0; i < NUM_K; i++) {
    current_k[i] = old_k[i] + (target_k[i] - old_k[i]) * effect / FR_SIZE;
  }
}

// silence, noise (pitch 0 or 1) or one pulse per pitch period
int32_t Vlm5030::excitation() {
  if (old_energy == 0) {
    return 0;
  }
  if (old_pitch <= 1) {
    rng = (rng >> 1) ^ (-(rng & 1) & 0xb400);
    return (rng & 1) ? current_energy : -current_energy;
  }
  return pitch_count == 0 ? current_energy : 0;
}

// MAME's division truncates toward zero, an arithmetic shift would not
int32_t Vlm5030::lattice(int32_t v) {
  int32_t u[NUM_K + 1];
  u[NUM_K] = v;
  for (int i = NUM_K - 1; i >= 0; i--) {
    u[i] = u[i + 1] - ((-current_k[i] * x[i]) / K_ONE);
  }
  for (int i = NUM_K - 1; i >= 1; i--) {
    x[i] = x[i - 1] + ((-current_k[i - 1] * u[i - 1]) / K_ONE);
  }
  x[0] = u[0];
  return u[0];
}

// MAME sound_stream_update for one sample. Its PH_SETUP / PH_WAIT /
// PH_END phases output nothing and only move BSY, so they fold into IDLE.
int16_t Vlm5030::sample() {
  if (phase == IDLE) {
    return 0;
  }

  if (sample_count == 0) {
    if (phase == STOP) {
      phase = IDLE;
      pin_bsy = false;
      return 0;
    }
    next_step();
  }

  int32_t out = lattice(excitation());

  sample_count--;
  pitch_count++;
  if (pitch_count >= current_pitch) {
    pitch_count = 0;
  }

  out = out > K_ONE - 1 ? K_ONE - 1 : (out < -K_ONE ? -K_ONE : out);
  return (int16_t)(out << SAMPLE_SHIFT);
}
