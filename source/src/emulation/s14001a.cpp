#include "s14001a.h"

// Ported from MAME s14001a.cpp. The START line is only ever pulsed by the
// caller, so MAME's m_bStart is always low inside Clock() and dropped here.

// pick two bits of rom data as delta
static uint8_t Mux8To2(bool bVoicedP2, uint8_t uPPQtrP2, uint8_t uDeltaAdrP2, uint8_t uRomDataP2) {
  if (bVoicedP2 && (uPPQtrP2 & 0x01)) { // mirroring
    uDeltaAdrP2 ^= 0x03; // count backwards
  }

  // 8 to 2 mux, big endian within the byte
  return uRomDataP2 >> (~uDeltaAdrP2 << 1 & 0x06) & 0x03;
}

static void CalculateIncrement(bool bVoicedP2, uint8_t uPPQtrP2, bool bPPQStartP2, uint8_t uDelta,
                               uint8_t uDeltaOldP2, uint8_t &uDeltaOldP1, uint8_t &uIncrementP2,
                               bool &bAddP2) {
  // beginning of a pitch period, voiced and unvoiced
  if ((uPPQtrP2 == 0x00) && bPPQStartP2) {
    uDeltaOldP2 = 0x02;
  }

  static const uint8_t uIncrements[4][4] = {
      // 00 01 10 11
      {3, 3, 1, 1}, // 00
      {1, 1, 0, 0}, // 01
      {0, 0, 1, 1}, // 10
      {1, 1, 3, 3}, // 11
  };

  const bool MIRROR = uPPQtrP2 & 0x01;

  // always computed, even when silent, to update uDeltaOld
  if (!bVoicedP2 || !MIRROR) {
    uIncrementP2 = uIncrements[uDelta][uDeltaOldP2];
    bAddP2 = uDelta >= 0x02;
  } else {
    uIncrementP2 = uIncrements[uDeltaOldP2][uDelta];
    bAddP2 = uDeltaOldP2 < 0x02;
  }
  uDeltaOldP1 = uDelta;
  if (bVoicedP2 && bPPQStartP2 && MIRROR) {
    uIncrementP2 = 0; // no change when first starting mirroring
  }
}

// limits output to 0x00..0x0f
static uint8_t CalculateOutput(bool bVoiced, bool bXSilence, uint8_t uPPQtr, bool bPPQStart,
                               uint8_t uLOutput, uint8_t uIncrementP2, bool bAddP2) {
  const bool SILENCE = uPPQtr & 0x02;

  if (bXSilence || (bVoiced && SILENCE)) {
    return 7;
  }

  // beginning of a pitch period, voiced and unvoiced
  if ((uPPQtr == 0x00) && bPPQStart) {
    uLOutput = 7;
  }

  uint8_t uTmp = uLOutput;
  if (!bAddP2) {
    uTmp ^= 0x0F; // turns subtraction into addition
  }

  // add 0, 1, 3; limit at 15
  uTmp += uIncrementP2;
  if (uTmp > 15) {
    uTmp = 15;
  }

  if (!bAddP2) {
    uTmp ^= 0x0F; // turns addition back to subtraction
  }

  return uTmp;
}

void S14001A::reset(const uint8_t *rom, uint16_t size) {
  *this = S14001A();
  m_rom = rom;
  m_romMask = size - 1;
}

uint8_t S14001A::clock() {
  // phase 2: latch phase 1 values, set up carries
  if (m_bPhase1) {
    m_bPhase1 = false;

    m_uStateP2 = m_uStateP1;
    m_uDAR13To05P2 = m_uDAR13To05P1;
    m_uDAR04To00P2 = m_uDAR04To00P1;
    m_uCWARP2 = m_uCWARP1;
    m_bStopP2 = m_bStopP1;
    m_bVoicedP2 = m_bVoicedP1;
    m_bSilenceP2 = m_bSilenceP1;
    m_uLengthP2 = m_uLengthP1;
    m_uXRepeatP2 = m_uXRepeatP1;
    m_uDeltaOldP2 = m_uDeltaOldP1;

    m_uOutputP2 = m_uOutputP1;
    m_uRomAddrP2 = m_RomAddrP1;

    m_bDAR04To00CarryP2 = m_uDAR04To00P2 == 0x1F;
    m_bPPQCarryP2 = m_bDAR04To00CarryP2 && ((m_uLengthP2 & 0x03) == 0x03); // pitch period quarter
    m_bRepeatCarryP2 = m_bPPQCarryP2 && ((m_uLengthP2 & 0x0C) == 0x0C);
    m_bLengthCarryP2 = m_bRepeatCarryP2 && (m_uLengthP2 == 0x7F);
    return m_uOutputP2;
  }
  m_bPhase1 = true;

  // phase 1 logic
  switch (m_uStateP1) {
  case IDLE:
    m_uOutputP1 = 7;
    m_bBusyP1 = false;
    break;

  case WORDWAIT:
    // word number into DAR bits 03..08, all other bits 0
    m_uDAR13To05P1 = (m_uWord & 0x3C) >> 2;
    m_uDAR04To00P1 = (m_uWord & 0x03) << 3;
    m_RomAddrP1 = (m_uDAR13To05P1 << 3) | (m_uDAR04To00P1 >> 2);
    m_uOutputP1 = 7;
    m_uStateP1 = CWARMSB;
    m_bBusyP1 = true;
    break;

  case CWARMSB:
    // control word address 8 MSBs, ROM address set up in the previous state
    m_uCWARP1 = readmem(m_uRomAddrP2) << 4;
    m_uDAR04To00P1 += 4;
    if (m_uDAR04To00P1 >= 32) {
      m_uDAR04To00P1 = 0; // 5 bit counter
    }
    m_RomAddrP1 = (m_uDAR13To05P1 << 3) | (m_uDAR04To00P1 >> 2);
    m_uOutputP1 = 7;
    m_uStateP1 = CWARLSB;
    break;

  case CWARLSB:
    m_uCWARP1 = m_uCWARP2 | (readmem(m_uRomAddrP2) >> 4);
    m_RomAddrP1 = m_uCWARP1;
    m_uOutputP1 = 7;
    m_uStateP1 = DARMSB;
    break;

  case DARMSB:
    m_uDAR13To05P1 = readmem(m_uRomAddrP2) << 1; // 9 bit counter, lsb zeroed
    m_uDAR04To00P1 = 0;
    m_uCWARP1++;
    m_RomAddrP1 = m_uCWARP1;
    m_uOutputP1 = 7;
    m_uStateP1 = CTRLBITS;
    break;

  case CTRLBITS: {
    const uint8_t ctrl = readmem(m_uRomAddrP2);
    m_bStopP1 = ctrl & 0x80;
    m_bVoicedP1 = ctrl & 0x40;
    m_bSilenceP1 = ctrl & 0x20;
    m_uXRepeatP1 = ctrl & 0x03;
    m_uLengthP1 = (ctrl & 0x1F) << 2; // includes external length and repeat
    m_uDAR04To00P1 = 0;
    m_uCWARP1++; // ready for the next DARMSB
    m_RomAddrP1 = (m_uDAR13To05P1 << 3) | (m_uDAR04To00P1 >> 2);
    m_uOutputP1 = 7;
    m_uStateP1 = PLAY;
    break;
  }

  case PLAY: {
    uint8_t uIncrementP2;
    bool bAddP2;
    const uint8_t uDeltaP2 = Mux8To2(m_bVoicedP2, m_uLengthP2 & 0x03, m_uDAR04To00P2 & 0x03,
                                     readmem(m_uRomAddrP2));
    CalculateIncrement(m_bVoicedP2, m_uLengthP2 & 0x03, m_uDAR04To00P2 == 0, uDeltaP2,
                       m_uDeltaOldP2, m_uDeltaOldP1, uIncrementP2, bAddP2);
    m_uOutputP1 = CalculateOutput(m_bVoicedP2, m_bSilenceP2, m_uLengthP2 & 0x03,
                                  m_uDAR04To00P2 == 0, m_uOutputP2, uIncrementP2, bAddP2);

    // advance counters
    m_uDAR04To00P1++;
    if (m_bDAR04To00CarryP2) { // pitch period quarter end
      m_uDAR04To00P1 = 0;      // 5 bit counter
      m_uLengthP1++;           // lower two bits count quarter pitch periods
      if (m_uLengthP1 >= 0x80) {
        m_uLengthP1 = 0; // 7 bit counter
      }
    }

    if (m_bVoicedP2 && m_bRepeatCarryP2) { // repeat complete
      m_uLengthP1 &= 0x70;                 // keep current length
      m_uLengthP1 |= (m_uXRepeatP1 << 2);  // reload repeat
      m_uDAR13To05P1++;                    // next 8 bytes
      if (m_uDAR13To05P1 >= 0x200) {
        m_uDAR13To05P1 = 0; // 9 bit counter
      }
    }
    if (!m_bVoicedP2 && m_bDAR04To00CarryP2) {
      // unvoiced advances each quarter pitch period, repeat not reloaded
      m_uDAR13To05P1++;
      if (m_uDAR13To05P1 >= 0x200) {
        m_uDAR13To05P1 = 0;
      }
    }

    m_RomAddrP1 = m_uDAR04To00P1;
    if (m_bVoicedP2 && (m_uLengthP1 & 0x1)) { // mirroring: count backwards
      m_RomAddrP1 ^= 0x1f;
    }
    m_RomAddrP1 = (m_uDAR13To05P1 << 3) | m_RomAddrP1 >> 2;

    if (m_bStopP2 && m_bLengthCarryP2) {
      m_uStateP1 = DELAY;
    } else if (m_bLengthCarryP2) {
      m_uStateP1 = DARMSB;
      m_RomAddrP1 = m_uCWARP1; // output correct address
    }
    break;
  }

  case DELAY:
    m_uOutputP1 = 7;
    m_uStateP1 = IDLE;
    break;
  }

  return m_uOutputP2;
}
