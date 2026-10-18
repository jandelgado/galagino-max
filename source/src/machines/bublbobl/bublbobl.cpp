#include "bublbobl.h"
#include "bublbobl_bank_rom.h"
#include "bublbobl_main_rom.h"
#include "bublbobl_sub_rom.h"
#include "bublbobl_tiles.h"
#include "bublbobl_tile_map.h"
#include "bublbobl_prom.h"
#include "bublbobl_logo.h"
#include "bublbobl_sid.h"
#include "bublbobl_samples.h"
#include "bublbobl_sound_map.h"
#include "../../emulation/arena.h"

using namespace bublbobl_video;

bublbobl::bublbobl() :
    bublbobl_bank_rom(bublbobl_bank_rom_blob),
    bublbobl_main_rom(bublbobl_main_rom_blob),
    bublbobl_sub_rom(bublbobl_sub_rom_blob),
    bublbobl_tiles(bublbobl_tiles_blob),
    bublbobl_tile_map(bublbobl_tile_map_blob),
    bublbobl_prom(bublbobl_prom_blob),
    bublbobl_sid(bublbobl_sid_blob) {
  // largest first: bank -> Arena block A, main + sub -> block B
  bank_rom = bublbobl_bank_rom.data();
  main_rom = bublbobl_main_rom.data();
  sub_rom = bublbobl_sub_rom.data();
  tiles = bublbobl_tiles.data();
  tile_map = bublbobl_tile_map.data();
  prom = bublbobl_prom.data();

  for (uint8_t t = 0; t < OBJ_TYPES; t++) {
    first_new[t] = first_new_column(prom, t);
  }

  // after the ROMs: the tune RAM fits next to the bank ROM in block A
  tune_ok = psidParse(bublbobl_sid.data(), bublbobl_sid.size(), &tune);
  if (tune_ok) {
    tune_ok = player.load(tune, Arena::alloc<uint8_t>(TUNE_RAM_SIZE), TUNE_RAM_SIZE);
  }
  if (!tune_ok) {
    printf("bublbobl: Bubble_Bobble.sid is not a supported PSID\n");
  }
}

const Logo &bublbobl::logo(void) {
  return bublbobl_logo;
}

void bublbobl::reset() {
  machineBase::reset();

  // /RESET clears the control latch: bank 4, sub CPU held, video off
  ctrl = 0;
  writeCtrl(0);
  sreset = false;
  irq_pending[MAIN_CPU] = false;
  irq_pending[SUB_CPU] = false;
  ic43_a = 0;
  ic43_b = 0;
  onSoundReset();
}

void bublbobl::writeCtrl(uint8_t value) {
  // sub CPU starts from 0 when released from reset
  if ((value & CTRL_SUB_RUN) && !(ctrl & CTRL_SUB_RUN)) {
    ResetZ80(&cpu[SUB_CPU]);
  }
  ctrl = value;

  if (value & CTRL_VIDEO_ON) {
    game_started = 1;
  }
}

unsigned char bublbobl::opZ80(unsigned short Addr) {
  return rdZ80(Addr);
}

unsigned char bublbobl::rdZ80(unsigned short Addr) {
  return current_cpu == MAIN_CPU ? rdMain(Addr) : rdSub(Addr);
}

void bublbobl::wrZ80(unsigned short Addr, unsigned char Value) {
  if (current_cpu == MAIN_CPU) {
    wrMain(Addr, Value);
    return;
  }

  if (Addr >= SHARE_ADDR && Addr < SHARE_END) {
    memory[Addr - RAM_BASE] = Value;
  }
}

unsigned char bublbobl::rdSub(unsigned short Addr) {
  if (Addr < BANK_ADDR) {
    return sub_rom[Addr];
  }
  if (Addr >= SHARE_ADDR && Addr < SHARE_END) {
    return memory[Addr - RAM_BASE];
  }
  return 0xff;
}

uint8_t bublbobl::in0() {
  const unsigned int keys = input->buttons_get();
  uint8_t v = (uint8_t)~(IN0_COIN1 | IN0_COIN2);

  if (keys & BUTTON_LEFT) {
    v &= ~IN0_LEFT;
  }
  if (keys & BUTTON_RIGHT) {
    v &= ~IN0_RIGHT;
  }
  // the game reads no up/down: stick up jumps
  if (keys & (BUTTON_UP | BUTTON_B)) {
    v &= ~IN0_JUMP;
  }
  if (keys & (BUTTON_FIRE | BUTTON_A)) {
    v &= ~IN0_BUBBLE;
  }
  if (keys & BUTTON_START) {
    v &= ~IN0_START1;
  }
  if (keys & BUTTON_COIN) {
    v |= IN0_COIN1;
  }
  return v;
}

unsigned char bublbobl::rdMain(unsigned short Addr) {
  if (Addr < BANK_ADDR) {
    return main_rom[Addr];
  }

  if (Addr < RAM_BASE) {
    const uint8_t bank = (ctrl ^ CTRL_BANK_XOR) & CTRL_BANK;
    return bank < BANKS ? bank_rom[bank * BANK_SIZE + (Addr - BANK_ADDR)] : 0x00;
  }

  if (Addr < IO_ADDR || (Addr >= RAM2_ADDR && Addr < IO2_ADDR)) {
    return memory[Addr - RAM_BASE];
  }
  return rdIo(Addr);
}

unsigned char bublbobl::rdIo(unsigned short Addr) {
  if (Addr < SOUND_IO_END) {
    // no sound CPU: nothing ever pending in either direction
    switch (Addr & 3) {
      case SOUND_LATCH: return 0x00;
      case SOUND_SEMAPHORE: return SEMAPHORE_IDLE;
      default: return 0xff;
    }
  }

  if ((Addr & ~3) == IC43_A_ADDR) {
    return ic43_a_r(Addr & 3);
  }
  if ((Addr & ~3) == IC43_B_ADDR) {
    return (Addr & 3) ? 0xff : (uint8_t)(ic43_b << 4);
  }

  switch (Addr) {
    case DSW0_ADDR: return BUBLBOBL_DSW0;
    case DSW1_ADDR: return BUBLBOBL_DSW1;
    case IN0_ADDR: return in0();
    case IN1_ADDR: return 0xff; // player 2, tilt, service: idle
  }
  return 0xff;
}

void bublbobl::wrMain(unsigned short Addr, unsigned char Value) {
  if (Addr < RAM_BASE) {
    return;
  }

  if (Addr < IO_ADDR || (Addr >= RAM2_ADDR && Addr < IO2_ADDR)) {
    memory[Addr - RAM_BASE] = Value;
    return;
  }
  wrIo(Addr, Value);
}

void bublbobl::wrIo(unsigned short Addr, unsigned char Value) {
  if (Addr < SOUND_IO_END) {
    if ((Addr & 3) == SOUND_LATCH) {
      onSoundCommand(Value);
    } else if ((Addr & 3) == SOUND_RESET) {
      const bool assert_reset = Value != 0;
      if (assert_reset && !sreset) {
        onSoundReset();
      }
      sreset = assert_reset;
    }
    return;
  }

  if (Addr < WATCHDOG_END) {
    return;
  }

  if (Addr < SUB_NMI_END) {
    if (ctrl & CTRL_SUB_RUN) {
      current_cpu = SUB_CPU;
      IntZ80(&cpu[SUB_CPU], INT_NMI);
      current_cpu = MAIN_CPU;
    }
    return;
  }

  if (Addr < CTRL_END) {
    writeCtrl(Value);
    return;
  }

  if ((Addr & ~3) == IC43_A_ADDR) {
    ic43_a_w(Addr & 3);
  } else if ((Addr & ~3) == IC43_B_ADDR) {
    ic43_b_w(Addr & 3, Value);
  }
}

// IC43 protection PAL, MAME boblbobl_ic43_*. Most checks are patched
// out in this bootleg; reads other than offset 0 are random.
uint8_t bublbobl::ic43_a_r(uint8_t offset) {
  if (offset == 0) {
    return (uint8_t)(ic43_a << 4);
  }
  rng = rng * 1103515245u + 12345u;
  return (uint8_t)(rng >> 16);
}

void bublbobl::ic43_a_w(uint8_t offset) {
  const uint8_t a = ic43_a;
  uint8_t res = 0;

  switch (offset) {
    case 0:
      if (~a & 8) { res ^= 1; }
      if (~a & 1) { res ^= 2; }
      if (~a & 1) { res ^= 4; }
      if (~a & 2) { res ^= 4; }
      if (~a & 4) { res ^= 8; }
      break;
    case 1:
      if (~a & 8) { res ^= 1; }
      if (~a & 2) { res ^= 1; }
      if (~a & 8) { res ^= 2; }
      if (~a & 1) { res ^= 4; }
      if (~a & 4) { res ^= 8; }
      break;
    case 2:
      if (~a & 4) { res ^= 1; }
      if (~a & 8) { res ^= 2; }
      if (~a & 2) { res ^= 4; }
      if (~a & 1) { res ^= 8; }
      if (~a & 4) { res ^= 8; }
      break;
    case 3:
      if (~a & 2) { res ^= 1; }
      if (~a & 4) { res ^= 2; }
      if (~a & 8) { res ^= 2; }
      if (~a & 8) { res ^= 4; }
      if (~a & 1) { res ^= 8; }
      break;
  }
  ic43_a = res;
}

void bublbobl::ic43_b_w(uint8_t offset, uint8_t data) {
  static const uint8_t xorval[4] = { 4, 1, 8, 2 };
  ic43_b = (data >> 4) ^ xorval[offset];
}

void bublbobl::stepCpu(uint8_t c) {
  current_cpu = c;
  StepZ80(&cpu[c]); StepZ80(&cpu[c]);
  StepZ80(&cpu[c]); StepZ80(&cpu[c]);
}

// in its idle loop with interrupts enabled: only an IRQ gets it out
bool bublbobl::isIdle(uint8_t c, uint16_t idle_pc) const {
  return cpu[c].PC.W == idle_pc && (cpu[c].IFF & IFF_1);
}

void bublbobl::run_frame(void) {
  for (uint16_t i = 0; i < SLICES; i++) {
    // irq0_line_hold: held until the CPU enables interrupts
    for (uint8_t c = 0; c < CPUS; c++) {
      if (irq_pending[c] && (cpu[c].IFF & IFF_1)) {
        current_cpu = c;
        IntZ80(&cpu[c], INT_RST38);
        irq_pending[c] = false;
      }
    }

    // idle decided per slice: a just delivered IRQ (PC $38) or a sub CPU
    // reset (PC 0), possibly mid-frame, resumes stepping
    const bool main_idle = isIdle(MAIN_CPU, MAIN_IDLE_PC);
    const bool sub_idle = !(ctrl & CTRL_SUB_RUN) || isIdle(SUB_CPU, SUB_IDLE_PC);
    if (main_idle && sub_idle) {
      break;
    }

    if (!main_idle) {
      stepCpu(MAIN_CPU);
    }
    if (!sub_idle) {
      stepCpu(SUB_CPU);
    }
  }

  vblank.publish([this](VideoState &v) {
    memcpy(v.obj, memory + OBJ_OFS, OBJ_SIZE);
    memcpy(v.pal, memory + PAL_OFS, PAL_SIZE);
    v.ctrl = ctrl;
  });

  irq_pending[MAIN_CPU] = true;
  irq_pending[SUB_CPU] = (ctrl & CTRL_SUB_RUN) != 0;
}

// RGBx_444 big endian -> RGB565, byte-swapped like the other machines
static uint16_t rgb444_to_565(uint8_t hi, uint8_t lo) {
  const uint8_t r = hi >> 4;
  const uint8_t g = hi & 0x0f;
  const uint8_t b = lo >> 4;
  const uint16_t v = (uint16_t)(((r << 1 | r >> 3) << 11) | ((g << 2 | g >> 2) << 5) | (b << 1 | b >> 3));
  return (uint16_t)((v << 8) | (v >> 8));
}

void bublbobl::prepare_frame(void) {
  vblank.read(video);

  for (uint16_t i = 0; i < 256; i++) {
    palette[i] = rgb444_to_565(video.pal[2 * i], video.pal[2 * i + 1]);
  }

  // MAME screen_update_bublbobl object walk; empty objects do not
  // advance the chained column x
  obj_count = 0;
  int16_t sx = 0;
  for (uint16_t offs = 0; offs < OBJ_SIZE; offs += 4) {
    const uint8_t *o = video.obj + offs;
    if (!(o[0] | o[1] | o[2] | o[3])) {
      continue;
    }

    const uint8_t gfx_num = o[1];
    const uint8_t attr = o[3];
    Obj &d = objs[obj_count++];
    d.type = gfx_num >> 5;
    d.gfx_offs = (uint16_t)((gfx_num & 0x1f) * 0x80 | ((gfx_num & 0xa0) == 0xa0 ? 0x1000 : 0));
    d.sy = (uint8_t)(-o[0]);
    d.bank = attr & 0x0f;
    d.pos.x_pre = sx;
    d.pos.x_set = (int16_t)(o[2] - ((attr & 0x40) ? 256 : 0));
    sx = next_sx(d.pos, first_new[d.type]);
  }
}

// one tile row (2 tiles side by side) of an object into the strip's pens
void bublbobl::drawObjectRow(const Obj &o, uint8_t yc, uint8_t line0) {
  const uint8_t e = prom[o.type * 16 + yc / 2];
  if (e & PROM_SKIP) {
    return;
  }

  const int16_t x0 = object_x(o.pos, first_new[o.type], yc);
  const uint8_t ytile = (uint8_t)(o.sy + yc * 8);
  const uint8_t *vram = memory; // $C000, live (see VideoState)

  for (uint8_t xc = 0; xc < 2; xc++) {
    const uint16_t goffs = o.gfx_offs + xc * 0x40 + (yc & 7) * 2 + (e & PROM_ROW_OFS) * 0x10;
    const uint8_t a = vram[goffs + 1];
    const uint16_t code = vram[goffs] + 256 * (a & 3) + 1024 * o.bank;
    if (code >= TILE_CODES) {
      continue;
    }
    const uint16_t t = tile_map[code];
    if (t == 0) {
      continue; // fully transparent
    }

    // flipped copies share one stored tile: combine its flips with the attribute's
    const uint32_t *rows = tiles[t & BUBLBOBL_TILE_INDEX];
    const uint8_t color = (uint8_t)((a & 0x3c) << 2);
    const bool flipx = (bool)(a & 0x40) != (bool)(t & BUBLBOBL_TILE_FLIPX);
    const bool flipy = (bool)(a & 0x80) != (bool)(t & BUBLBOBL_TILE_FLIPY);
    const int16_t x = x0 + xc * 8;

    for (uint8_t tl = 0; tl < 8; tl++) {
      const uint8_t sl = (uint8_t)(ytile + tl - line0);
      if (sl >= 8) {
        continue;
      }

      const uint32_t bits = rows[flipy ? 7 - tl : tl];
      uint8_t *dst = pens[sl];
      for (uint8_t px = 0; px < 8; px++) {
        const int16_t X = x + px;
        if (X < 0 || X > 255) {
          continue;
        }
        const uint8_t pen = (bits >> (4 * (flipx ? 7 - px : px))) & 0x0f;
        if (pen != PEN_TRANSPARENT) {
          dst[X] = color | pen;
        }
      }
    }
  }
}

void bublbobl::render_row(short row) {
  // frame_buffer is already black outside the game and with video off
  if (row < FIRST_ROW || row >= LAST_ROW || !(video.ctrl & CTRL_VIDEO_ON)) {
    return;
  }

  // objects in MAME order, later ones on top
  const uint8_t line0 = (uint8_t)(VIS_TOP + 8 * (row - FIRST_ROW));
  memset(pens, PEN_BG, sizeof(pens));
  for (uint8_t i = 0; i < obj_count; i++) {
    uint8_t yc[2];
    const uint8_t n = rows_for_strip(objs[i].sy, line0, yc);
    for (uint8_t r = 0; r < n; r++) {
      drawObjectRow(objs[i], yc[r], line0);
    }
  }

  // 256 native columns -> OUT_WIDTH
  for (uint8_t l = 0; l < 8; l++) {
    unsigned short *out = frame_buffer + l * OUT_WIDTH;
    for (uint16_t c = 0; c < OUT_WIDTH; c++) {
      out[c] = palette[pens[l][native_x(c)]];
    }
  }
}

// next request for the audio core: sequence + 1, subtune
static uint16_t nextRequest(uint16_t request, uint8_t subtune) {
  return (uint16_t)((((request >> 8) + 1) << 8) | subtune);
}

// emulation core: music commands switch the SID tune, effect commands
// queue a recorded sample
void bublbobl::onSoundCommand(uint8_t cmd) {
  const uint8_t music = bublbobl_music[cmd];
  if (music) {
    const uint8_t subtune = music == STOP_MUSIC ? 0 : music;
    if (subtune != music_subtune) {
      music_subtune = subtune;
      music_request = nextRequest(music_request, subtune);
    }
  }

  const uint8_t effect = bublbobl_effect[cmd];
  if (effect != NO_EFFECT) {
    fx_ring.push(effect);
  }
}

// emulation core: sound CPU reset silences music and effects
void bublbobl::onSoundReset() {
  music_subtune = 0;
  music_request = nextRequest(music_request, 0);
  reset_request.fetch_add(1, std::memory_order_release);
}

// audio core: SID music plus up to two effect samples, +-512
void bublbobl::renderSid(int16_t *out, int n) {
  const uint8_t resets = reset_request.load(std::memory_order_acquire);
  if (resets != reset_seen) {
    reset_seen = resets;
    fx.stopAll();
    uint8_t dropped;
    while (fx_ring.pop(dropped)) {
    }
  }

  // tune changes run the 6502 init here, on the audio core
  const uint16_t request = music_request;
  if (tune_ok && request != music_seen) {
    music_seen = request;
    const uint8_t subtune = request & 0xff;
    sid_on = subtune != 0;
    if (sid_on) {
      player.start(subtune);
    }
  }

  uint8_t e;
  while (fx_ring.pop(e)) {
    const FlashAsset<unsigned char, PLAIN> &b = *bublbobl_fx_blobs[e];
    fx.start({ b.data, b.count }, e);
  }

  if (sid_on) {
    player.render(out, n);
  } else {
    memset(out, 0, n * sizeof(*out));
  }

  for (int i = 0; i < n; i++) {
    const int32_t v = out[i] + FX_GAIN * fx.next();
    out[i] = (int16_t)(v > SAMPLE_MAX ? SAMPLE_MAX : (v < -SAMPLE_MAX ? -SAMPLE_MAX : v));
  }
}

#ifdef LED_PIN
void bublbobl::menuLeds(CRGB *leds) {
  defaultMenuLeds(leds);
}
#endif
