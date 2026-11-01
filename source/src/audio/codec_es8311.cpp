// Register setup ported from Espressif's es8311 driver (esp-bsp,
// SPDX-FileCopyrightText: 2015-2022 Espressif Systems (Shanghai) CO LTD,
// SPDX-License-Identifier: Apache-2.0), formerly lib/es8311. Same register
// writes for the configuration galagino uses.
#include "codec_es8311.h"
#include "../hal/hal.h"

namespace {

const uint8_t REG_RESET = 0x00;
const uint8_t REG_CLK_MANAGER_01 = 0x01;  // MCLK source, clock enables
const uint8_t REG_CLK_MANAGER_02 = 0x02;  // pre divider, multiplier
const uint8_t REG_CLK_MANAGER_03 = 0x03;  // ADC fs mode, OSR
const uint8_t REG_CLK_MANAGER_04 = 0x04;  // DAC OSR
const uint8_t REG_CLK_MANAGER_05 = 0x05;  // ADC/DAC dividers
const uint8_t REG_CLK_MANAGER_06 = 0x06;  // BCLK polarity, divider
const uint8_t REG_CLK_MANAGER_07 = 0x07;  // LRCK divider high
const uint8_t REG_CLK_MANAGER_08 = 0x08;  // LRCK divider low
const uint8_t REG_SDP_IN = 0x09;
const uint8_t REG_SDP_OUT = 0x0A;
const uint8_t REG_SYSTEM_0D = 0x0D;  // power up analog
const uint8_t REG_SYSTEM_0E = 0x0E;  // PGA, ADC modulator
const uint8_t REG_SYSTEM_12 = 0x12;  // DAC power
const uint8_t REG_SYSTEM_13 = 0x13;  // headphone driver
const uint8_t REG_ADC_1C = 0x1C;     // ADC equalizer, DC offset
const uint8_t REG_DAC_VOLUME = 0x32;
const uint8_t REG_DAC_37 = 0x37;     // DAC equalizer bypass

const uint32_t MCLK_PER_SAMPLE = 256;
const uint8_t SDP_16_BIT = 3 << 2;
const uint32_t RESET_MS = 20;

// Clock dividers of one MCLK / rate pair (driver table coeff_div).
struct Coeff {
  uint32_t mclk;
  uint32_t rate;
  uint8_t preDiv, preMulti, adcDiv, dacDiv, fsMode, lrckH, lrckL, bclkDiv, adcOsr, dacOsr;
};

const Coeff COEFFS[] = {
  {6144000, 24000, 0x01, 0x00, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
};

int bus;
uint8_t chip;

bool put(uint8_t reg, uint8_t value) {
  const uint8_t data[2] = {reg, value};
  return hal::I2c::write(bus, chip, data, 2);
}

bool get(uint8_t reg, uint8_t &value) {
  return hal::I2c::writeRead(bus, chip, &reg, 1, &value, 1);
}

const Coeff *findCoeff(uint32_t mclk, uint32_t rate) {
  for (const Coeff &c : COEFFS) {
    if (c.mclk == mclk && c.rate == rate) {
      return &c;
    }
  }
  return nullptr;
}

// Read-modify-write of `reg`: keep the bits in `keep`, or in `bits`.
bool update(uint8_t reg, uint8_t keep, uint8_t bits) {
  uint8_t v;
  return get(reg, v) && put(reg, (uint8_t)((v & keep) | bits));
}

bool configDividers(const Coeff &c) {
  const uint8_t bclk = (uint8_t)(c.bclkDiv < 19 ? c.bclkDiv - 1 : c.bclkDiv);
  return update(REG_CLK_MANAGER_02, 0x07, (uint8_t)((c.preDiv - 1) << 5 | c.preMulti << 3)) &&
         put(REG_CLK_MANAGER_03, (uint8_t)(c.fsMode << 6 | c.adcOsr)) &&
         put(REG_CLK_MANAGER_04, c.dacOsr) &&
         put(REG_CLK_MANAGER_05, (uint8_t)((c.adcDiv - 1) << 4 | (c.dacDiv - 1))) &&
         update(REG_CLK_MANAGER_06, 0xE0, bclk) &&
         update(REG_CLK_MANAGER_07, 0xC0, c.lrckH) &&
         put(REG_CLK_MANAGER_08, c.lrckL);
}

// MCLK from its pin, not inverted; BCLK not inverted.
bool configClocks(const Coeff &c) {
  return put(REG_CLK_MANAGER_01, 0x3F) && update(REG_CLK_MANAGER_06, (uint8_t)~0x20, 0) &&
         configDividers(c);
}

// Slave serial port, 16 bit in and out.
bool configFormat() {
  return update(REG_RESET, 0xBF, 0) && put(REG_SDP_IN, SDP_16_BIT) && put(REG_SDP_OUT, SDP_16_BIT);
}

bool powerUp() {
  return put(REG_SYSTEM_0D, 0x01) && put(REG_SYSTEM_0E, 0x02) && put(REG_SYSTEM_12, 0x00) &&
         put(REG_SYSTEM_13, 0x10) && put(REG_ADC_1C, 0x6A) && put(REG_DAC_37, 0x08);
}

// 0..100 to the 0..255 register scale of the driver.
uint8_t volumeReg(uint8_t volume) {
  if (volume > 100) {
    volume = 100;
  }
  return volume == 0 ? 0 : (uint8_t)(volume * 256 / 100 - 1);
}

} // namespace

bool Es8311::init(int port, uint8_t addr, uint32_t rate, uint8_t volume) {
  bus = port;
  chip = addr;
  const Coeff *coeff = findCoeff(rate * MCLK_PER_SAMPLE, rate);
  if (!coeff) {
    return false;
  }

  // reset to defaults, then power on
  if (!put(REG_RESET, 0x1F)) {
    return false;
  }
  hal::sleepMs(RESET_MS);

  return put(REG_RESET, 0x00) && put(REG_RESET, 0x80) && configClocks(*coeff) && configFormat() && powerUp() &&
         put(REG_DAC_VOLUME, volumeReg(volume));
}
