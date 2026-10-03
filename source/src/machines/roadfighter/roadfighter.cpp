// ============================================================================
// roadfighter.cpp — Road Fighter (Konami 1984) port SPINNERINO
// FASE 2: CPU M6809 (KONAMI-1) + memory map roadf + audio Z80 + run_frame.
//   Modellato su CONSOLE_QUADRA_NANO/hyperolympic.cpp (stessa famiglia Konami).
//   Video (tilemap+sprite) e mix audio = FASI 3-4 (render_row ancora nero).
// ============================================================================
#include "roadfighter.h"

#ifdef ENABLE_ROADFIGHTER

#include "roadfighter_rom_main.h"
#include "roadfighter_rom_audio.h"
#include "roadfighter_tiles.h"
#include "roadfighter_sprites.h"

roadfighter::roadfighter() {
	// In ctor: m6809_reset() reads the reset vector through these.
	main_raw_ptr = roadfighter_rom_main_raw.data();
	audio_rom_ptr = roadfighter_rom_audio.data();
}

roadfighter::~roadfighter() {
	roadfighter_rom_main_raw.release();
	roadfighter_rom_audio.release();
	roadfighter_tiles.release();
	roadfighter_sprites.release();
}

/*
// ---- callback bus M6809 (puntatori a funzione globali della destinazione) ----
static uint8_t roadf_m6809_read(uint16_t addr) {
  return g_roadfighter_instance ? g_roadfighter_instance->main_read(addr) : 0xFF;
}
static void roadf_m6809_write(uint16_t addr, uint8_t val) {
  if (g_roadfighter_instance) g_roadfighter_instance->main_write(addr, val);
}
static uint8_t roadf_m6809_read_opcode(uint16_t addr) {
  return g_roadfighter_instance ? g_roadfighter_instance->main_read_opcode(addr) : 0xFF;
}

static void roadf_set_m6809_callbacks() {
  m6809_read_fn   = roadf_m6809_read;          // dati/operandi -> RAW
  m6809_write_fn  = roadf_m6809_write;
  m6809_opcode_fn = roadf_m6809_read_opcode;   // opcode-fetch -> DECRYPTED (KONAMI-1)
}
*/

void roadfighter::init(Input *input, unsigned short *framebuffer,
                       sprite_S *spritebuffer, unsigned char *memorybuffer) {
  machineBase::init(input, framebuffer, spritebuffer, memorybuffer);
  //g_roadfighter_instance = this;
  //roadf_set_m6809_callbacks();
}

void roadfighter::start(void) {
}

void roadfighter::reset() {
  machineBase::reset();
  //g_roadfighter_instance = this;
  //roadf_set_m6809_callbacks();

  memset(&main_cpu, 0, sizeof(main_cpu));
  memset(snd_ram, 0, sizeof(snd_ram));
  sound_latch = 0;
  sn_latch = 0;
  sn_latch_reg = 0;
  snd_icnt = 0;
  dac_sample = 0;
  irq_mask = 0;
  flip_screen = 0;
  // Latches start armed so COIN/START held from the menu launch registers
  // only after release; else the boot self-test reports "D BAD".
  coin_latch = 1;
  coin_hold = 0;
  start_latch = 1;
  start_hold = 0;
  gear_state = 0;
  prev_fire_btn = 0;

  current_cpu = 0;
  ResetZ80(&cpu[0]);          // audio Z80
  m6809_reset(&main_cpu);     // legge reset vector $FFFE (via main_read -> ROM raw)

  // instruction fetches straight from ROM, skipping the per-byte callback
  // chain; must follow m6809_reset(), which clears the window
  main_cpu.rom_direct = main_raw_ptr;
  main_cpu.rom_base = ROADF_ROM_BASE;
  main_cpu.rom_size = ROADF_ROM_SIZE;
  main_cpu.konami1 = 1;
}

// ============================================================
// Main KONAMI-1 (M6809) memory map
// ============================================================

uint8_t roadfighter::m6809_read(m6809_state *s, uint16_t addr) {
  if (addr >= 0x4000)
    return main_raw_ptr[addr - 0x4000];

  // I/O reads (PRIMA del catch-all RAM, altrimenti il gioco legge RAM stantia)
  if (addr == 0x1600) return ROADF_DSW2;
  if (addr >= 0x1680 && addr <= 0x1683) {
    switch (addr & 0x03) {
      case 0: return input_system();
      case 1: return input_p1();
      // P2: bit6 = ACTIVE_HIGH (idle=0), tutti gli altri ACTIVE_LOW (idle=1) -> 0xBF.
      // CRITICO: con 0xFF (bit6=1) il gioco fa il check NVRAM a $3F08 vs firma ROM
      // $96E6 (che fallisce, NVRAM vuota) e va in HANG infinito a $57A5 (sub-state 4).
      case 2: return 0xBF;
      case 3: return ROADF_DSW1;
    }
  }

  // RAM: sprite/scroll $1000-$10FF + VRAM/CRAM/work/NVRAM $2000-$3FFF
  return memory[addr];
}

// ROM opcodes ($4000+) never get here: the core fetches and KONAMI-1
// decrypts them from the rom_direct window installed in reset().
uint8_t roadfighter::m6809_read_opcode(m6809_state *s, uint16_t addr) {
  return m6809_read(s, addr);
}

void roadfighter::m6809_write(m6809_state *s, uint16_t addr, uint8_t val) {
  if (addr >= 0x4000) return;        // ROM: ignora

  if (addr == 0x1400) return;        // watchdog reset: no-op

  if (addr >= 0x1480 && addr <= 0x1487) {
    // LS259 mainlatch: bit0 di val -> Q[addr&7]
    int bit = val & 1;
    switch (addr & 0x07) {
      case 0: flip_screen = bit; break;                 // Q0
      case 1: if (bit) IntZ80(&cpu[0], INT_RST38); break; // Q1 sound-on -> IRQ audio Z80
      case 3: /* coin counter 1 */ break;               // Q3
      case 4: /* coin counter 2 */ break;               // Q4
      case 7:                                            // Q7 = irq_mask main
        irq_mask = bit;
        if (!irq_mask) main_cpu.irq_pending = 0;
        break;
      default: break;
    }
    return;
  }

  if (addr == 0x1500) { sound_latch = val; return; }

  // RAM
  memory[addr] = val;
  if (addr >= ROADF_VRAM_OFF && addr < 0x3000) game_started = 1;
}

// ============================================================
// Inputs (FASE 5 = sterzo EC11 fine + tarature). Per ora base:
//   $1680 system: bit0 COIN1, bit3 START1 (ACTIVE_LOW)
//   $1681 P1: bit0-3 joystick sx/dx, bit4 BUTTON1 (LOW gear), bit5 BUTTON2 (HIGH gear)
// ============================================================

unsigned char roadfighter::input_system() {
  unsigned char keymask = input->buttons_get();
  unsigned char val = 0xFF;

  if ((keymask & BUTTON_COIN) && !coin_latch) { coin_latch = 1; coin_hold = 45; }
  if (!(keymask & BUTTON_COIN)) coin_latch = 0;

  if ((keymask & BUTTON_START) && !start_latch) { start_latch = 1; start_hold = 45; }
  if (!(keymask & BUTTON_START)) start_latch = 0;

  if (coin_hold)  val &= ~0x01;   // COIN1
  if (start_hold) val &= ~0x08;   // START1
  return val;
}

unsigned char roadfighter::input_p1() {
  unsigned char keymask = input->buttons_get();
  unsigned char val = 0xFF;
  // Mapping provvisorio (rifinito in FASE 5 con EC11->sterzo digitale sx/dx).
  if (keymask & BUTTON_LEFT)  val &= ~0x01;   // EC11 INVERTITO -> sterzo destra
  if (keymask & BUTTON_RIGHT) val &= ~0x02;   // EC11 INVERTITO -> sterzo sinistra
  // Acceleratore a TOGGLE (stato calcolato in run_frame):
  //   gear_state 0 = marcia LENTA (BUTTON1), 1 = marcia VELOCE (BUTTON2)
  if (gear_state == 0) val &= ~0x10;          // BUTTON1 = marcia lenta
  else                 val &= ~0x20;          // BUTTON2 = marcia veloce
  return val;
}

// ============================================================
// Audio Z80 map (roadf sound_map):
//   $0000-$3FFF ROM (8 KB mirror)  $4000-$4FFF RAM
//   $6000 sound latch read         $8000 timer (trackfld_audio)
//   $E000 DAC write   $E001 SN76489A latch   $E002 SN76489A strobe
// ============================================================

unsigned char roadfighter::opZ80(unsigned short Addr) { return rdZ80(Addr); }

unsigned char roadfighter::rdZ80(unsigned short Addr) {
  if (Addr < 0x4000)
    return audio_rom_ptr[Addr & 0x1FFF];
  if ((Addr & 0xF000) == 0x4000)
    return snd_ram[Addr & 0x0FFF];
  if (Addr == 0x6000)
    return sound_latch;
  if (Addr == 0x8000)
    return (snd_icnt >> 8) & 0x0F;     // timer (approssimato come hyperolympic)
  return 0xFF;
}

void roadfighter::wrZ80(unsigned short Addr, unsigned char Value) {
  if ((Addr & 0xF000) == 0x4000) { snd_ram[Addr & 0x0FFF] = Value; return; }
  if (Addr == 0xE000) { dac_sample = (int)Value - 128; return; }  // DAC (mix in FASE 4)
  if (Addr == 0xE001) { sn_latch = Value; return; }               // SN76489 data latch
  if (Addr == 0xE002) { sn76489_write(sn_latch); return; }        // SN76489 strobe
}

unsigned char roadfighter::inZ80(unsigned short Port) { return 0xFF; }
void roadfighter::outZ80(unsigned short Port, unsigned char Value) { }

// SN76489 byte-stream -> framework sn_*[0] (mix vero in audio.cpp, FASE 4)
void roadfighter::sn76489_write(unsigned char data) {
  if (data & 0x80) {
    sn_latch_reg = (data >> 4) & 0x07;
    unsigned char val4 = data & 0x0F;
    int ch = sn_latch_reg >> 1;
    if (sn_latch_reg & 1) {
      sn_volume[0][ch] = val4; sn_min_volume[0][ch] = val4;
      if (val4 < 15) sn_hold[0][ch] = 6;
    } else {
      sn_period[0][ch] = (sn_period[0][ch] & 0x3F0) | val4;
    }
  } else {
    int ch = sn_latch_reg >> 1;
    if (sn_latch_reg & 1) {
      unsigned char val4 = data & 0x0F;
      sn_volume[0][ch] = val4; sn_min_volume[0][ch] = val4;
      if (val4 < 15) sn_hold[0][ch] = 6;
    } else {
      sn_period[0][ch] = (sn_period[0][ch] & 0x00F) | ((data & 0x3F) << 4);
    }
  }
}

// ============================================================
// Frame: M6809 @1.536MHz -> 25600 cicli/frame, Z80 audio interleaved.
// IRQ0 main a fine frame (vblank) se irq_mask.
// ============================================================

void roadfighter::run_frame(void) {
  // Acceleratore a TOGGLE su FIRE: ogni tap (premi+lascia il pomello EC11) alterna
  //   marcia LENTA <-> marcia VELOCE (sempre in accelerazione, niente folle).
  // Edge detection sul RILASCIO, 1 volta per frame (qui), cosi' non sfarfalla con
  // le letture multiple di $1681 fatte dalla CPU durante il frame.
  unsigned char fire_now = (input->buttons_get() & BUTTON_FIRE) ? 1 : 0;
  if (prev_fire_btn && !fire_now)
    gear_state ^= 1;
  prev_fire_btn = fire_now;

  static const int M6809_CYCLES_PER_FRAME = 25600;
  static const int SLICES_PER_FRAME = 720;
  // ~5000 Z80 steps/frame, as circusc
  static const int Z80_STEPS_PER_SLICE = 7;
  // rdZ80 $8000 reads snd_icnt>>8: 21*720/256 = 59 ticks/frame
  static const int SND_TIMER_PER_SLICE = 21;
  int m6809_cycles = 0;
  int safety = 0;
  const int SAFETY_LIMIT = 80000;

  // Equals (CYCLES*(i+1))/SLICES_PER_FRAME without a per-slice divide.
  const int slice_step = M6809_CYCLES_PER_FRAME / SLICES_PER_FRAME;
  const int slice_step_rem = M6809_CYCLES_PER_FRAME % SLICES_PER_FRAME;
  int slice_target = 0;
  int slice_rem_acc = 0;

  for (int i = 0; i < SLICES_PER_FRAME; i++) {
    slice_target += slice_step;
    slice_rem_acc += slice_step_rem;
    if (slice_rem_acc >= SLICES_PER_FRAME) { slice_rem_acc -= SLICES_PER_FRAME; slice_target++; }
    while (m6809_cycles < slice_target && safety < SAFETY_LIMIT) {
      int c = m6809_step(&main_cpu, 1);
      if (c <= 0) c = 1;
      m6809_cycles += c;
      safety++;
    }
    current_cpu = 0;

    // Tempo follows the $8000 timer, not the Z80 step count; keep it at
    // MAME's rate (1 tick per 1024 cycles). Same sound board as circusc.
    for (int z = 0; z < Z80_STEPS_PER_SLICE; z++) {
      StepZ80(&cpu[0]);
    }
    snd_icnt += SND_TIMER_PER_SLICE;
  }

  vblank.publish([this](VideoState &s) {
    memcpy(s.vram,   memory + ROADF_VRAM_OFF,   sizeof(s.vram));
    memcpy(s.cram,   memory + ROADF_CRAM_OFF,   sizeof(s.cram));
    memcpy(s.scroll, memory + ROADF_SCROLL_OFF, sizeof(s.scroll));
    memcpy(s.spr,    memory + ROADF_SPRRAM_OFF, sizeof(s.spr));
    s.flip_screen = flip_screen;
  });

  if (irq_mask)
    m6809_irq(&main_cpu);

  dbg_pc = main_cpu.PC;        // cattura PC per overlay (se bloccato = indirizzo del loop)

  if (coin_hold)  coin_hold--;
  if (start_hold) start_hold--;
}

// ============================================================
// Video — roadf (ROT90, like zaxxon/galaxian).
// Tilemap 64x32, scroll PER-RIGA (32 righe), visarea y 16..239 (tile row +2).
//   tile code = vram | (cram&0x80)<<1 | (cram&0x60)<<4; color=cram&0x0f; flipx=cram&0x10
// Sprite (48, draw_sprites base_state): code=byte2+8*(flags&0x20), color=flags&0x0f,
//   flipx=~flags&0x40, flipy=flags&0x80, sx=byte3, sy=241-byte1, doppio draw a sx e sx-256.
// ============================================================

void roadfighter::prepare_frame(void) {
  vblank.read(video);

  // Popola sprite[] in coordinate buffer (y in 0..223 = game_y - 16).
  active_sprites = 0;
  for (int offs = 0xC0 - 4; offs >= 0 && active_sprites < 96; offs -= 4) {
    unsigned char flags  = video.spr[offs + 0];
    unsigned char sy_raw = video.spr[offs + 1];
    unsigned char code_l = video.spr[offs + 2];
    unsigned char sx     = video.spr[offs + 3];
    if ((flags | sy_raw | code_l | sx) == 0) continue;   // slot vuoto

    // flip_screen MAME: sy = 240-(240-sy_raw)=sy_raw, poi +1; flipy invertito. flipx invariato.
    int flipy = (flags & 0x80) ? 1 : 0;
    int sy;
    if (video.flip_screen) { sy = (int)sy_raw + 1; flipy = !flipy; }
    else             { sy = 241 - (int)sy_raw; }
    sprite_S &sp = sprite[active_sprites];
    sp.code  = (unsigned short)((code_l + 8 * (flags & 0x20)) & (ROADF_NSPRITES - 1));
    sp.color = flags & 0x0F;
    sp.flags = (((flags & 0x40) == 0) ? 1 : 0)        // flipx (~flags & 0x40), NON cambia con flip_screen
             | (flipy ? 2 : 0);
    short native_x = (short)sx;          // band axis after rotation
    short native_sy = (short)(sy - 16);  // 0..223

    // Sprite axes differ from render_row (verified on hardware): column is
    // flipped, band is direct. The other forms put sprites on the wrong X
    // and made cars drive downward.
    // sp.flags stay in native axes: blit_sprite_strip indexes gfx by native
    // axis, so swapping the bits would mirror sprites.
    sp.x = (short)(223 - (int)native_sy);
    sp.y = native_x;
    active_sprites++;
  }
}

// Overlay di debug (P4 senza seriale): top 5 righe mostrano diagnostica.
//   riga 0: barra VERDE = #byte VRAM non-zero (0..2048 scalato a 256px)
//   riga 1: barra CIANO = #byte CRAM non-zero
//   riga 2: PC del M6809 (16 px, bianco = bit 1) — deve CAMBIARE se la CPU gira
//   riga 3: ROSSO se irq_mask=1, GIALLO se game_started
//   riga 4: nera (separatore)
// Disattivare mettendo a 0 dopo la diagnosi.
#define ROADF_DEBUG_OVERLAY 0
// Broken since ROT90: writes ROADF_SCREEN_W px/row into a ROADF_RENDER_W
// frame_buffer and corrupts the heap. Resize before enabling.

void roadfighter::render_row(short row) {
#if ROADF_DEBUG_OVERLAY
  if (row < 5) {
    int vnz = 0;
    for (int i = 0; i < 0x800; i++) if (video.vram[i]) vnz++;
    unsigned short pc = dbg_pc;          // PC catturato STABILE in run_frame
    unsigned char hi = pc >> 8, lo = pc & 0xFF;
    for (int line = 0; line < 8; line++) {
      unsigned short *p = frame_buffer + line * ROADF_SCREEN_W;
      for (int x = 0; x < ROADF_SCREEN_W; x++) p[x] = 0x0000;
      if (row == 0) { int w = vnz >> 3; for (int x = 0; x < w && x < 256; x++) p[x] = 0xE007; }   // verde = VRAM
      // PC byte ALTO (riga 1) e BASSO (riga 2): 8 quadrati SEMPRE visibili,
      // bianco = bit 1, grigio scuro = bit 0 (MSB a sinistra/in alto).
      else if (row == 1) { for (int b = 0; b < 8; b++) { unsigned short c = (hi & (0x80 >> b)) ? 0xFFFF : 0x0841;
                           for (int x = 0; x < 28; x++) p[b * 32 + x] = c; } }
      else if (row == 2) { for (int b = 0; b < 8; b++) { unsigned short c = (lo & (0x80 >> b)) ? 0xFFFF : 0x0841;
                           for (int x = 0; x < 28; x++) p[b * 32 + x] = c; } }
      else if (row == 3) { unsigned short c = irq_mask ? 0x00F8 : 0x0000; for (int x = 0; x < 128; x++) p[x] = c;
                           unsigned short g = game_started ? 0xE0FF : 0x0000; for (int x = 128; x < 256; x++) p[x] = g; }
    }
    return;
  }
#endif
#define ROADF_TEST_TILES 0
#if ROADF_TEST_TILES
  // TEST: riempi lo schermo con tile consecutivi (IGNORA la VRAM) per validare
  // la pipeline di render. Se vedi una griglia di caratteri/grafica leggibile e
  // colorata -> render OK (e il viola del gioco e' contenuto VRAM, non bug mio).
  // Se vedi viola/garbage -> pipeline rotta. Mettere a 0 dopo la diagnosi.
  for (int line = 0; line < 8; line++) {
    unsigned short *ptr = frame_buffer + line * ROADF_SCREEN_W;
    for (int x = 0; x < ROADF_SCREEN_W; x++) {
      int tcol = x >> 3;
      int code = (row * 32 + tcol) % ROADF_NTILES;     // tile consecutivi
      const unsigned short *cm = roadfighter_tile_colormap[(tcol + 1) & 0x0F];
      uint32_t pr = roadfighter_tiles[code][line];
      unsigned char pix = (pr >> ((x & 7) * 4)) & 0x0F;
      ptr[x] = cm[pix];
    }
  }
  return;
#endif

  // Band index 0..31 (raw native X / 8); 36 physical strips give a 2-band
  // letterbox top+bottom (see zaxxon.cpp's identical tcol convention).
  const int tcol = row - 2;
  if (tcol < 0 || tcol >= 32) return;   // caller already memset this strip to 0

  // Band = 255-native_X, column = native_Y. Per-row scroll varies with j:
  // native Y maps to the column axis.
  for (int sub_y = 0; sub_y < 8; sub_y++) {
    int rx = 255 - (tcol * 8 + sub_y);
    unsigned short *ptr = frame_buffer + sub_y * ROADF_RENDER_W;

    int prev_tile_row = -1, game_x = 0;
    const unsigned short *cmap = nullptr;
    const uint32_t *tile_gfx = nullptr;
    int flipx = 0;

    for (int j = 0; j < ROADF_RENDER_W; j++) {
      int ry = j + 16;                                  // raw native Y, 16..239
      int tile_row = ry >> 3;                           // 2..29

      if (tile_row != prev_tile_row) {                 // ricarica GFX 1x ogni 8 j (tile row)
        int srow = tile_row * 2;
        int scrollx = video.scroll[srow] | ((video.scroll[srow + 1] & 0x01) << 8);
        if (video.flip_screen) scrollx = -scrollx;             // MAME: flip_screen nega lo scroll
        game_x = (rx + scrollx) & 0x1FF;                // tilemap 512 wide wrap
        int tile_col = (game_x >> 3) & 63;
        int ti = (tile_row * 64 + tile_col) & 0x7FF;
        unsigned char v = video.vram[ti];
        unsigned char a = video.cram[ti];
        unsigned int c = (unsigned)v | (((unsigned)a & 0x80) << 1) | (((unsigned)a & 0x60) << 4);
        if (c >= ROADF_NTILES) c %= ROADF_NTILES;
        tile_gfx = roadfighter_tiles[c];
        cmap  = roadfighter_tile_colormap[a & 0x0F];
        flipx = a & 0x10;
        prev_tile_row = tile_row;
      }

      uint32_t prow = tile_gfx[ry & 7];  // sub-row within tile varies every j
      int tile_c = game_x & 7;
      int src_c  = flipx ? (7 - tile_c) : tile_c;
      unsigned char pix = (prow >> (src_c * 4)) & 0x0F;
      ptr[j] = cmap[pix];
    }
  }

  for (unsigned char s = 0; s < active_sprites; s++)
    blit_sprite_strip(tcol, s);
}

// Native dx sweeps the band (direct), dy the column (flipped).
void roadfighter::blit_sprite_strip(int tcol, unsigned char s) {
  sprite_S &sp = sprite[s];
  int band_top = tcol * 8;

  int code = sp.code;
  int flipx = sp.flags & 1;
  int flipy = sp.flags & 2;
  const uint32_t *gfx = roadfighter_sprites[code];
  const unsigned short *cmap = roadfighter_sprite_colormap[sp.color];

  for (int dx = 0; dx < 16; dx++) {
    int band0 = sp.y + dx;
    int band1 = sp.y - 256 + dx;                        // wrap (2o draw MAME, sx-256)
    int sub_y;
    if (band0 >= band_top && band0 < band_top + 8) sub_y = band0 - band_top;
    else if (band1 >= band_top && band1 < band_top + 8) sub_y = band1 - band_top;
    else continue;                                      // sprite non in questa banda

    int sx_src = flipx ? (15 - dx) : dx;
    unsigned short *ptr = frame_buffer + sub_y * ROADF_RENDER_W;

    for (int dy = 0; dy < 16; dy++) {
      int col = sp.x - dy;
      if (col < 0 || col >= ROADF_RENDER_W) continue;
      int sy_src = flipy ? (15 - dy) : dy;
      uint32_t gw = gfx[sy_src * 2 + (sx_src >= 8 ? 1 : 0)];
      unsigned char pix = (gw >> ((sx_src & 7) * 4)) & 0x0F;
      if (!pix) continue;                               // pen 0 = trasparente
      ptr[col] = cmap[pix];
    }
  }
}

#endif // ENABLE_ROADFIGHTER
