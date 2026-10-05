#ifndef COMMANDO_H
#define COMMANDO_H

#include "commando_logo.h"
#include "../machineBase.h"
#include "../../emulation/sidplayer.h"

// Test drive, not the arcade game: plays Rob Hubbard's C64 Commando
// tune (romszip/Commando.sid) to judge SID cost and sound before the
// real port. Black screen. UP/DOWN: subtune, FIRE: restart. Serial
// prints the subtune and the render cost once per second.
class commando : public machineBase {
protected:
  // the whole .sid file, unpacked into the Arena on first data()
  Asset<unsigned char, COMPRESSED> commando_sid;
public:
  commando();
  signed char machineType() override { return MCH_COMMANDO; }
  void start() override;
  void run_frame() override;
  void render_row(short row) override;
  void renderSid(int16_t *out, int n) override;
  static const Logo &logo() { return commando_logo; }
#ifdef LED_PIN
  static void menuLeds(CRGB *leds);
  void gameLeds(CRGB *leds) override;
#endif

private:
  static const uint16_t TUNE_RAM_SIZE = 0x1000; // $5000-$5FFF, see test_sidplayer
  static const uint32_t PROFILE_BUFFERS = 375;  // 64 samples each = 1 s
  static const uint32_t BUFFER_BUDGET = 640000; // 240 MHz x 64 / 24 kHz

  SidPlayer player;
  PsidInfo tune = {};
  uint8_t *tune_ram = nullptr;
  bool tune_ok = false;

  // written by run_frame() (emulation core), read by renderSid() (audio)
  volatile uint8_t song_request = 1;
  volatile uint8_t restart_request = 0;

  // audio core only
  uint8_t song_playing = 0, restart_seen = 0;
  uint32_t prof_cycles = 0, prof_buffers = 0;

  unsigned int last_buttons = 0;
};

#endif
