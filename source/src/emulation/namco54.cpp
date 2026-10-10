#if defined(ENABLE_GALAGA) || defined(ENABLE_XEVIOUS)

#include "namco54.h"
#include "../machines/galaga/namco54_rom.h"
#include <math.h>

// MB8844 clock: MASTER_CLOCK / 6 / 2 = 1.536 MHz, one machine cycle per
// 6 clocks. The 06xx runs at MASTER_CLOCK / 6 / 64 = 48 kHz.
static const uint32_t MCU_HZ = 1536000 / 6;
static const uint32_t SAMPLE_HZ = 24000;
static const uint32_t N06XX_HZ = 48000;

// galaga_a.cpp component values
static const float V_REF = 5.0f * 2.2f / (3.3f + 2.2f);
static const float V_DAC_ON = 4.0f;
static const float V_MAX = 5.0f - 1.5f;          // vP - OP_AMP_VP_RAIL_OFFSET
static const float LADDER[4] = { 47e3f, 22e3f, 10e3f, 4.7e3f };
static const float MIX_R[3] = { 33e3f, 33e3f, 10e3f };
static const float MIX_RF = 3.3e3f;
static const float MIX_C_AMP = 0.1e-6f;
static const float MIX_GAIN = 40800.0f;

// MAME's 16-bit output scale -> galagino's +-512. Galaga explosion peak
// ~384, like the former sample (x3 on the 8-bit scale), WSG headroom.
static const float OUT_SCALE = 1.0f / 72.0f;

Namco54::Namco54() : Mb88(namco54_rom_data) {
  float g = 0;
  for (int bit = 0; bit < 4; bit++) {
    g += 1.0f / LADDER[bit];
  }
  const float r_dac = 1.0f / g;

  for (int i = 0; i < 16; i++) {
    float cur = 0;
    for (int bit = 0; bit < 4; bit++) {
      if (i & (1 << bit)) {
        cur += V_DAC_ON / LADDER[bit];
      }
    }
    dac[i] = cur * r_dac;
  }

  chanl[0].set(r_dac + 100e3f, 22e3f, 220e3f, 0.001e-6f);
  chanl[1].set(r_dac + 47e3f, 10e3f, 150e3f, 0.01e-6f);
  chanl[2].set(r_dac + 150e3f, 22e3f, 470e3f, 0.01e-6f);
  hp_k = 1.0f - expf(-1.0f / (SAMPLE_HZ * 100e3f * MIX_C_AMP));
  Mb88::reset();
}

void Namco54::write(uint8_t data, uint8_t ctrl) {
  const uint8_t h = head.load(std::memory_order_relaxed);
  const uint8_t next = (h + 1) & (QUEUE_SIZE - 1);
  if (next == tail.load(std::memory_order_acquire)) {
    return;
  }
  queue[h] = data | ((ctrl >> 5) << 8);
  head.store(next, std::memory_order_release);
}

void Namco54::hold(bool reset) {
  reset_line.store(reset, std::memory_order_release);
}

void Namco54::write_o(uint8_t value, uint8_t mask) {
  if (mask == 0x0f) {
    out[0] = value & 0x0f;
  } else {
    out[1] = value >> 4;
  }
}

void Namco54::write_r(uint8_t port, uint8_t value) {
  if (port == 1) {
    out[2] = value & 0x0f;
  }
}

void Namco54::feed(int cycles) {
  if (irq_left > 0) {
    irq_left -= cycles;
    if (irq_left <= 0) {
      set_irq(false);
    }
  }
  if (gap > 0) {
    gap -= cycles;
    return;
  }

  const uint8_t t = tail.load(std::memory_order_relaxed);
  if (t == head.load(std::memory_order_acquire)) {
    return;
  }
  const uint16_t entry = queue[t];
  tail.store((t + 1) & (QUEUE_SIZE - 1), std::memory_order_release);

  // one chip select pulse per 06xx period, high for half of it
  cmd = entry & 0xff;
  gap = (int)(MCU_HZ * (1u << (entry >> 8)) / N06XX_HZ);
  irq_left = gap / 2;
  set_irq(true);
}

int16_t Namco54::render() {
  if (reset_line.load(std::memory_order_acquire)) {
    // held chip: restart from reset, drop bytes sent meanwhile
    if (!in_reset) {
      Mb88::reset();
      set_irq(false);
      in_reset = true;
    }
    tail.store(head.load(std::memory_order_acquire), std::memory_order_release);
    gap = irq_left = 0;
  } else {
    in_reset = false;
    phase += MCU_HZ;
    const int cycles = phase / SAMPLE_HZ;
    phase -= cycles * SAMPLE_HZ;
    feed(cycles);
    run(cycles);
  }

  // CHANL1 <- R1, CHANL2 <- O7-O4, CHANL3 <- O3-O0
  const float v = mix(chanl[0].step(dac[out[2]]),
                      chanl[1].step(dac[out[1]]),
                      chanl[2].step(dac[out[0]]));
  return (int16_t)(v * OUT_SCALE);
}

// MAME DISC_OP_AMP_FILTER_IS_BAND_PASS_1M: input r_in, bias r_bias to
// vRef, feedback r_f, both caps c. Bilinear, pre-warped.
void Namco54::BandPass::set(float r_in, float r_bias, float r_f, float c) {
  const float r_total = 1.0f / (1.0f / r_in + 1.0f / r_bias);
  const float fc = 1.0f / (2.0f * (float)M_PI * sqrtf(r_total * r_f * c * c));
  const float d = 2.0f * c / sqrtf(r_f / r_total * c * c);
  const float gain = -r_f / r_total * 0.5f;

  const float two_over_t = 2.0f * SAMPLE_HZ;
  const float wc = SAMPLE_HZ * 2.0f * tanf((float)M_PI * fc / SAMPLE_HZ);
  const float den = two_over_t * two_over_t + d * wc * two_over_t + wc * wc;
  a1 = 2.0f * (-two_over_t * two_over_t + wc * wc) / den;
  a2 = (two_over_t * two_over_t - d * wc * two_over_t + wc * wc) / den;
  b0 = gain * d * wc * two_over_t / den;

  // input current through r_in, Millman over r_in || r_bias
  b0 *= r_total / r_in;
  x1 = x2 = y1 = y2 = 0;
}

float Namco54::BandPass::step(float v_dac) {
  const float v = v_dac - V_REF;
  float y = -a1 * y1 - a2 * y2 + b0 * (v - x2) + V_REF;
  x2 = x1;
  x1 = v;
  y2 = y1;
  if (y > V_MAX) {
    y = V_MAX;
  }
  if (y < 0) {
    y = 0;
  }
  y1 = y - V_REF;
  return y;
}

// MAME DISC_MIXER_IS_OP_AMP with output coupling cap cAmp (100k load)
float Namco54::mix(float c1, float c2, float c3) {
  float v = ((V_REF - c1) / MIX_R[0] + (V_REF - c2) / MIX_R[1] + (V_REF - c3) / MIX_R[2]) * MIX_RF;
  hp += (v - hp) * hp_k;
  return (v - hp) * MIX_GAIN;
}

#endif
