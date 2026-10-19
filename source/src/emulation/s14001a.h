#ifndef S14001A_H
#define S14001A_H

#include <stdint.h>

// SSi TSI S14001A speech chip (MAME devices/sound/s14001a.cpp, Ed Bernard
// rewrite). Delta modulation from a speech ROM, 4 bit output. The caller
// drives the clock: one clock() per chip clock edge pair, MAME Clock().
//
//   data_w(word) + start()   speak word 0..63
//   busy()                   /BUSY, until 3 clocks after the word ends
//   clock()                  output 0..15, 7 = silence
class S14001A {
public:
  void reset(const uint8_t *rom, uint16_t size); // size: power of two
  void data_w(uint8_t number) { m_uWord = number & 0x3f; }
  // START pulsed high then low (MAME start_w(1); start_w(0))
  void start() { m_uStateP1 = WORDWAIT; }
  bool busy() const { return m_bBusyP1; }
  uint8_t clock();

private:
  enum State : uint8_t { IDLE, WORDWAIT, CWARMSB, CWARLSB, DARMSB, CTRLBITS, PLAY, DELAY };

  uint8_t readmem(uint16_t offset) const { return m_rom[offset & 0xfff & m_romMask]; }

  const uint8_t *m_rom = nullptr;
  uint16_t m_romMask = 0;

  bool m_bPhase1 = false;
  State m_uStateP1 = IDLE, m_uStateP2 = IDLE;
  uint16_t m_uDAR13To05P1 = 0, m_uDAR13To05P2 = 0; // 9 bit counter
  uint16_t m_uDAR04To00P1 = 0, m_uDAR04To00P2 = 0; // 5 bit counter
  uint16_t m_uCWARP1 = 0, m_uCWARP2 = 0;           // 12 bit counter
  bool m_bStopP1 = false, m_bStopP2 = false;
  bool m_bVoicedP1 = false, m_bVoicedP2 = false;
  bool m_bSilenceP1 = false, m_bSilenceP2 = false;
  uint8_t m_uLengthP1 = 0, m_uLengthP2 = 0;        // 7 bit: length, repeat, pitch quarter
  uint8_t m_uXRepeatP1 = 0, m_uXRepeatP2 = 0;
  uint8_t m_uDeltaOldP1 = 0, m_uDeltaOldP2 = 0;
  uint8_t m_uOutputP1 = 7, m_uOutputP2 = 7;
  uint16_t m_RomAddrP1 = 0, m_uRomAddrP2 = 0;
  bool m_bDAR04To00CarryP2 = false, m_bPPQCarryP2 = false;
  bool m_bRepeatCarryP2 = false, m_bLengthCarryP2 = false;
  bool m_bBusyP1 = false;
  uint8_t m_uWord = 0;
};

#endif
