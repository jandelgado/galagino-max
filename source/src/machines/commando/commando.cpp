#include "commando.h"
#include "commando_sid.h"
#include "../../emulation/arena.h"

commando::commando() : commando_sid(commando_sid_blob) { }

void commando::start() {
  if (tune_ram) {
    return; // start() runs twice
  }

  tune_ok = psidParse(commando_sid.data(), commando_sid.size(), &tune);
  if (!tune_ok) {
    printf("commando: Commando.sid is not a supported PSID\n");
    return;
  }
  tune_ram = Arena::alloc<uint8_t>(TUNE_RAM_SIZE);
  tune_ok = player.load(tune, tune_ram, TUNE_RAM_SIZE);
  song_request = tune.start_song;
}

void commando::run_frame() {
  game_started = 1;

  const unsigned int keys = input->buttons_get();
  const unsigned int pressed = keys & ~last_buttons;
  last_buttons = keys;

  // EXTRA + UP/DOWN is the global volume control
  if (!tune_ok || (keys & BUTTON_EXTRA)) {
    return;
  }

  uint8_t song = song_request;
  if (pressed & BUTTON_UP) {
    song = song < tune.songs ? song + 1 : 1;
  }
  if (pressed & BUTTON_DOWN) {
    song = song > 1 ? song - 1 : tune.songs;
  }
  if (pressed & BUTTON_FIRE) {
    restart_request++;
  }
  song_request = song;
}

void commando::render_row(short row) {
  memset(frame_buffer, 0, renderBuffer());
}

void commando::renderSid(int16_t *out, int n) {
  if (!tune_ok) {
    memset(out, 0, n * sizeof(*out));
    return;
  }

  // song change: init runs here, on the audio core, never concurrently
  const uint8_t song = song_request, restart = restart_request;
  if (song != song_playing || restart != restart_seen) {
    song_playing = song;
    restart_seen = restart;
    player.start(song);
    printf("commando: song %u/%u\n", song, tune.songs);
  }

  const uint32_t t0 = ESP.getCycleCount();
  player.render(out, n);
  prof_cycles += ESP.getCycleCount() - t0;

  if (++prof_buffers == PROFILE_BUFFERS) {
    const uint32_t avg = prof_cycles / PROFILE_BUFFERS;
    printf("commando: %u cycles/buffer, %u.%u%% of a core, strays %u\n",
           (unsigned)avg, (unsigned)(avg * 100 / BUFFER_BUDGET),
           (unsigned)(avg * 1000 / BUFFER_BUDGET % 10), (unsigned)player.strays());
    prof_cycles = prof_buffers = 0;
  }
}

#ifdef LED_PIN
// logo colors
static const CRGB SAND = CRGB(0xc8a85a);
static const CRGB OLIVE = CRGB(0x6b8e23);

void commando::menuLeds(CRGB *leds) {
  const CRGB pattern[NUM_LEDS] = {OLIVE, SAND, OLIVE, SAND, OLIVE, SAND, OLIVE};
  memcpy(leds, pattern, sizeof(pattern));
}

void commando::gameLeds(CRGB *leds) {
  menuLeds(leds);
}
#endif
