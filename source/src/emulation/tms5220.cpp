#include "tms5220.h"
#include <string.h>

// MAME tms5110r.hxx tms5220_coeff: TI_028X_LATER_ENERGY, TI_5220_PITCH,
// TI_5110_5220_LPC, TI_LATER_CHIRP, TI_INTERP
static const uint8_t ENERGY_BITS = 4, PITCH_BITS = 6;
static const uint8_t KBITS[10] = {5, 5, 4, 4, 4, 4, 4, 3, 3, 3};

static const uint8_t ENERGY[16] = {0, 1, 2, 3, 4, 6, 8, 11, 16, 23, 33, 47, 63, 85, 114, 0};

static const uint8_t PITCH[64] = {
    0,  15, 16, 17, 18, 19, 20,  21,  22,  23,  24,  25,  26,  27,  28,  29,
    30, 31, 32, 33, 34, 35, 36,  37,  38,  39,  40,  41,  42,  44,  46,  48,
    50, 52, 53, 56, 58, 60, 62,  65,  68,  70,  72,  76,  78,  80,  84,  86,
    91, 94, 98, 101, 105, 109, 114, 118, 122, 127, 132, 137, 142, 148, 153, 159};

static const int16_t KTABLE[10][32] = {
    {-501, -498, -497, -495, -493, -491, -488, -482, -478, -474, -469, -464, -459, -452, -445, -437,
     -412, -380, -339, -288, -227, -158, -81, -1, 80, 157, 226, 287, 337, 379, 411, 436},
    {-328, -303, -274, -244, -211, -175, -138, -99, -59, -18, 24, 64, 105, 143, 180, 215,
     248, 278, 306, 331, 354, 374, 392, 408, 422, 435, 445, 455, 463, 470, 476, 506},
    {-441, -387, -333, -279, -225, -171, -117, -63, -9, 45, 98, 152, 206, 260, 314, 368},
    {-328, -273, -217, -161, -106, -50, 5, 61, 116, 172, 228, 283, 339, 394, 450, 506},
    {-328, -282, -235, -189, -142, -96, -50, -3, 43, 90, 136, 182, 229, 275, 322, 368},
    {-256, -212, -168, -123, -79, -35, 10, 54, 98, 143, 187, 232, 276, 320, 365, 409},
    {-308, -260, -212, -164, -117, -69, -21, 27, 75, 122, 170, 218, 266, 314, 361, 409},
    {-256, -161, -66, 29, 124, 219, 314, 409},
    {-256, -176, -96, -15, 65, 146, 226, 307},
    {-205, -132, -59, 14, 87, 160, 234, 307},
};

static const uint8_t CHIRP_LAST = 51; // the chirp address stops counting here
static const int8_t CHIRP[CHIRP_LAST + 1] = {
    0x00, 0x03, 0x0f, 0x28, 0x4c, 0x6c, 0x71, 0x50, 0x25, 0x26, 0x4c, 0x44, 0x1a,
    0x32, 0x3b, 0x13, 0x37, 0x1a, 0x25, 0x1f, 0x1d, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// interpolation period 1..7, 0: shift of the step towards the new frame
static const uint8_t INTERP[8] = {0, 3, 3, 3, 2, 2, 1, 1};

// commands (outside Speak External), bits 4-6
enum : uint8_t { CMD_MASK = 0x70, CMD_SPEAK_EXTERNAL = 0x60, CMD_RESET = 0x70 };

// rs_ws: /RS level bit 1, /WS level bit 0; a cycle is its strobe low
enum : uint8_t { RS_HIGH = 0x02, WS_HIGH = 0x01 };
enum : uint8_t { RSWS_WRITE = RS_HIGH, RSWS_READ = WS_HIGH, RSWS_IDLE = RS_HIGH | WS_HIGH };

// chip clocks from /WS low to READY (FIFO write), from /RS low (13 us at
// 672 kHz, MAME) and per output sample
static const uint8_t WS_READY_CLOCKS = 16, RS_READY_CLOCKS = 9, CLOCKS_PER_SAMPLE = 80;

static const uint8_t SUBC_RELOAD = 1;   // 1: normal speed, 0: SPKSLOW (51xx)
static const uint8_t PC_LAST = 12;      // parameter counter: E, P, K1..K10, frame load
static const uint8_t IP_LAST = 7;
static const uint8_t BUFFER_LOW_MAX = 8; // fifo_count at or below: BL
static const int16_t IDLE_OUTPUT = -1;   // datasheet table 4

static int32_t wrap(int32_t v, int32_t bits) {
  const int32_t half = 1 << (bits - 1);
  return ((v + half) & ((half << 1) - 1)) - half;
}

// k is a signed 10 bit, the running value a signed 14 bit number
static int32_t matrix_multiply(int32_t k, int32_t v) {
  return (wrap(k, 10) * wrap(v, 15)) >> 9;
}

// 14 bit filter output to the 8 bit DAC, range extended to 16 bits
static int16_t clip_analog(int32_t v) {
  v = v > 2047 ? 2047 : (v < -2048 ? -2048 : v);
  v &= ~0xf;
  return (int16_t)((v << 4) | ((v & 0x7f0) >> 3) | ((v & 0x400) >> 10));
}

void Tms5220::reset(SpeechRing *out, uint16_t cps, uint32_t now) {
  ring = out;
  cycles_per_sample = cps;
  next_sample = now;
  ready_pending = false;
  io_ready = true;
  rs_ws = RSWS_IDLE;
  read_latch = 0xff;
  write_latch = 0;
  chip_reset();
}

// MAME device_reset(), also the reset command
void Tms5220::chip_reset() {
  memset(fifo, 0, sizeof(fifo));
  fifo_head = fifo_tail = fifo_count = fifo_bits_taken = 0;

  spen = ddis = talk = talkd = previous_talk_status = false;
  buffer_empty = buffer_low = true;

  new_energy_idx = new_pitch_idx = 0;
  memset(new_k_idx, 0, sizeof(new_k_idx));
  current_energy = current_pitch = 0;
  previous_energy = 0;
  memset(current_k, 0, sizeof(current_k));
  zpar = uv_zpar = false;

  inhibit = true;
  pitch_zero = false;
  subcycle = pc = 0;
  pitch_count = 0;
  olde = oldp = true;
  ip = 0;
  rng = 0x1fff;
  excitation = 0;
  memset(u, 0, sizeof(u));
  memset(x, 0, sizeof(x));
}

void Tms5220::ready_cycle(uint8_t clocks, uint32_t now) {
  update(now);
  io_ready = false;
  ready_pending = true;
  ready_at = now + (uint32_t)cycles_per_sample * clocks / CLOCKS_PER_SAMPLE;
}

// MAME wsq_w/rsq_w: a falling strobe starts a ready cycle
void Tms5220::strobe(uint8_t next, uint8_t clocks, uint32_t now) {
  if (next == rs_ws) {
    return;
  }
  const bool falling = (rs_ws & ~next) != 0;
  rs_ws = next;
  if (next == 0) {
    return; // both low: illegal on a TMS5220
  }
  if (next == RSWS_IDLE) {
    read_latch = 0xff;
    return;
  }
  if (falling) {
    ready_cycle(clocks, now);
  }
}

void Tms5220::wsq_w(bool level, uint32_t now) {
  strobe((rs_ws & RS_HIGH) | (level ? WS_HIGH : 0), WS_READY_CLOCKS, now);
}

void Tms5220::rsq_w(bool level, uint32_t now) {
  strobe((rs_ws & WS_HIGH) | (level ? RS_HIGH : 0), RS_READY_CLOCKS, now);
}

uint8_t Tms5220::status_r(uint32_t now) {
  update(now);
  return rs_ws == RSWS_READ ? read_latch : 0xff;
}

bool Tms5220::readyq_r(uint32_t now) {
  update(now);
  return !io_ready;
}

// READY timer: the write or read the strobe started completes. A write
// to a full FIFO retries every 16 clocks.
void Tms5220::io_ready_event() {
  ready_pending = false;
  if (rs_ws == RSWS_WRITE) {
    if (fifo_count >= FIFO_SIZE && ddis) {
      ready_pending = true;
      ready_at += (uint32_t)cycles_per_sample * WS_READY_CLOCKS / CLOCKS_PER_SAMPLE;
      return;
    }
    data_write(write_latch);
  } else if (rs_ws == RSWS_READ) {
    read_latch = status_read();
  }
  io_ready = true;
}

void Tms5220::update(uint32_t now) {
  while (ready_pending && (int32_t)(now - ready_at) >= 0) {
    while ((int32_t)(ready_at - next_sample) >= 0) {
      ring->push(process());
      next_sample += cycles_per_sample;
    }
    io_ready_event();
  }
  while ((int32_t)(now - next_sample) >= 0) {
    ring->push(process());
    next_sample += cycles_per_sample;
  }
}

// silence parameters until the first frame is parsed
void Tms5220::zero_new_frame() {
  zpar = uv_zpar = true;
  olde = oldp = true;
  new_energy_idx = new_pitch_idx = 0;
  for (uint8_t i = 0; i < NUM_K; i++) {
    new_k_idx[i] = i < 4 ? 0 : (i < 7 ? 0xf : 0x7);
  }
}

void Tms5220::data_write(uint8_t data) {
  if (!ddis) {
    process_command(data);
    return;
  }
  if (fifo_count >= FIFO_SIZE) {
    return;
  }

  const bool old_buffer_low = buffer_low;
  fifo[fifo_tail] = data;
  fifo_tail = (fifo_tail + 1) % FIFO_SIZE;
  fifo_count++;
  fifo_status();

  // BL falling edge: enough data to start talking
  if (!spen && old_buffer_low && !buffer_low) {
    zero_new_frame();
    spen = true;
    talk = true; // MAME FAST_START_HACK
  }
}

// Speak External and Reset; the speech ROM (VSM) commands are NOPs
void Tms5220::process_command(uint8_t cmd) {
  switch (cmd & CMD_MASK) {
    case CMD_SPEAK_EXTERNAL:
      memset(fifo, 0, sizeof(fifo));
      fifo_head = fifo_tail = fifo_count = fifo_bits_taken = 0;
      ddis = true;
      zero_new_frame();
      break;
    case CMD_RESET:
      chip_reset();
      break;
  }
  fifo_status();
}

// BL, BE, and TS falling, which ends Speak External
void Tms5220::fifo_status() {
  buffer_low = fifo_count <= BUFFER_LOW_MAX;
  buffer_empty = fifo_count == 0;
  if (buffer_empty && ddis) {
    talk = spen = false;
  }

  if (previous_talk_status && !talk_status()) {
    ddis = false;
  }
  previous_talk_status = talk_status();
}

// MSB first from the FIFO bytes, LSB first; floats high without data
int Tms5220::extract_bits(int count) {
  if (!ddis) {
    return (1 << count) - 1;
  }

  int val = 0;
  while (count--) {
    val = (val << 1) | ((fifo[fifo_head] >> fifo_bits_taken) & 1);
    fifo_bits_taken++;
    if (fifo_bits_taken >= 8) {
      fifo_count--;
      fifo[fifo_head] = 0; // read past the end gives zeros
      fifo_head = (fifo_head + 1) % FIFO_SIZE;
      fifo_bits_taken = 0;
      fifo_status();
    }
  }
  return val;
}

uint8_t Tms5220::status_read() const {
  return (talk_status() << 7) | (buffer_low << 6) | (buffer_empty << 5);
}

// A frame running out of data stops on BE like a stop frame does.
void Tms5220::parse_frame() {
  uv_zpar = zpar = false;
  ip = 0;

  fifo_status();
  if (ddis && buffer_empty) {
    return;
  }

  new_energy_idx = extract_bits(ENERGY_BITS);
  if (ddis && buffer_empty) {
    return;
  }
  if (new_frame_silence() || new_frame_stop()) {
    return;
  }

  const int repeat = extract_bits(1);
  new_pitch_idx = extract_bits(PITCH_BITS);
  uv_zpar = new_frame_unvoiced(); // unvoiced: K5-K10 zero
  if ((ddis && buffer_empty) || repeat) {
    return;
  }

  // unvoiced frames carry K1-K4 only
  const uint8_t num_k = new_frame_unvoiced() ? 4 : NUM_K;
  for (uint8_t i = 0; i < num_k; i++) {
    new_k_idx[i] = extract_bits(KBITS[i]);
    if (ddis && buffer_empty) {
      return;
    }
  }
}

// 10 stage lattice, chip order (MAME, US patent 4,209,804)
int32_t Tms5220::lattice_filter() {
  u[10] = matrix_multiply(previous_energy, excitation << 6);
  for (int i = 9; i >= 0; i--) {
    u[i] = u[i + 1] - matrix_multiply(current_k[i], x[i]);
  }
  for (int i = 9; i >= 1; i--) {
    x[i] = x[i - 1] + matrix_multiply(current_k[i - 1], u[i - 1]);
  }
  x[0] = u[0];
  previous_energy = current_energy;
  return u[0];
}

// one sample (MAME process(), size 1)
int16_t Tms5220::process() {
  if (!talkd) {
    subcycle++;
    if (subcycle == 2 && pc == PC_LAST) {
      if (ip == IP_LAST) {
        talkd = talk;
        fifo_status();
        if (!talk && spen) {
          talk = true;
        }
      }
      subcycle = SUBC_RELOAD;
      pc = 0;
      ip = (ip + 1) & 7;
    } else if (subcycle == 3) {
      subcycle = SUBC_RELOAD;
      pc++;
    }
    return IDLE_OUTPUT;
  }

  if (ip == 0 && pc == PC_LAST && subcycle == 1) {
    parse_frame();
    if (new_frame_stop()) {
      talk = spen = false;
      fifo_status();
    }

    // no interpolation across voiced/unvoiced or out of silence
    inhibit = (!oldp && new_frame_unvoiced()) || (oldp && !new_frame_unvoiced()) ||
              (olde && !new_frame_silence()) || (oldp && new_frame_silence());
  } else if (subcycle == 2) {
    // B cycle of PC 0..11: step E, P or K1..K10 towards the new frame
    const int on = !(inhibit && ip != 0);
    const uint8_t shift = INTERP[ip];
    if (pc == 0) {
      if (ip == 0) {
        pitch_zero = false;
      }
      current_energy = (current_energy + (((ENERGY[new_energy_idx] - current_energy) * on) >> shift)) * !zpar;
    } else if (pc == 1) {
      current_pitch = (current_pitch + (((PITCH[new_pitch_idx] - current_pitch) * on) >> shift)) * !zpar;
    } else if (pc < PC_LAST) {
      const uint8_t k = pc - 2;
      const bool zero = k < 4 ? zpar : uv_zpar;
      current_k[k] = (current_k[k] + (((KTABLE[k][new_k_idx[k]] - current_k[k]) * on) >> shift)) * !zero;
    }
  }

  // unvoiced: noise of half the chirp peak, voiced: chirp
  if (oldp) {
    excitation = (rng & 1) ? ~0x3f : 0x40;
  } else {
    excitation = CHIRP[pitch_count >= CHIRP_LAST ? CHIRP_LAST : pitch_count];
  }

  // LFSR clocks once per T cycle, 20 per sample
  for (int i = 0; i < 20; i++) {
    const uint16_t bit = ((rng >> 12) ^ (rng >> 3) ^ (rng >> 2) ^ rng) & 1;
    rng = (rng << 1) | bit;
  }

  // the K1 stage may overflow 14 bits
  const int16_t out = clip_analog(wrap(lattice_filter(), 15));

  subcycle++;
  if (subcycle == 2 && pc == PC_LAST) {
    if (ip == IP_LAST) {
      if (inhibit) {
        pitch_zero = true;
      }
      olde = new_frame_silence();
      oldp = new_frame_unvoiced();
      talkd = talk;
      fifo_status();
      if (!talk && spen) {
        talk = true;
      }
    }
    subcycle = SUBC_RELOAD;
    pc = 0;
    ip = (ip + 1) & 7;
  } else if (subcycle == 3) {
    subcycle = SUBC_RELOAD;
    pc++;
  }

  pitch_count++;
  if (pitch_count >= current_pitch || pitch_zero) {
    pitch_count = 0;
  }
  pitch_count &= 0x1ff;
  return out;
}
