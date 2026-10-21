#include "circusc.h"
#include "circusc_main_rom.h"
#include "circusc_audio_rom.h"
#include "circusc_tilemap.h"
#include "circusc_spritemap.h"
#include "circusc_cmap.h"

circusc::circusc()
  : circusc_audio_rom(circusc_audio_rom_blob),
    circusc_main_rom(circusc_main_rom_blob),
    circusc_spritemap(circusc_spritemap_blob),
    circusc_tilemap(circusc_tilemap_blob) {
	// In ctor: m6809_reset() reads the reset vector through these.
	main_rom_ptr = circusc_main_rom.data();
	audio_rom_ptr = circusc_audio_rom.data();
}

void circusc::reset() {
  machineBase::reset();
  memset(spr_buf, 0, sizeof(spr_buf));
  memset(snd_ram, 0, sizeof(snd_ram));
  irq_mask   = 0;
  spritebank = 0;
  scroll     = 0;
  soundlatch = 0;
  snd_irq_pend = 0;
  sn_latch = 0;
  snd_icnt = 0;
  dac_wr = dac_rd = 0;
  dac_val = dac_last = 128;
  dac_acc = 0;
  m6809_reset(&main_cpu);   // vettore reset da 0xFFFE (ROM, KONAMI-1)

  // instruction fetches straight from ROM, decrypted by the core, skipping
  // the per-byte callback chain; must follow m6809_reset(), which clears it
  main_cpu.rom_direct = main_rom_ptr;
  main_cpu.rom_base = 0x6000;
  main_cpu.rom_size = 0xA000;
  main_cpu.konami1 = 1;
}

void circusc::start() {
  reset();
  game_started = 1;
}

// ============================================================
// KONAMI-1: XOR degli opcode dipendente dall'indirizzo (identico a
// rocnrope: mask = (a&0x02 ? 0x80:0x20) | (a&0x08 ? 0x08:0x02), solo
// per fetch dalla ROM >= 0x6000).
// ============================================================
unsigned char GAME_IRAM circusc::m6809_read_opcode(m6809_state *s, uint16_t addr) {
  if (addr >= 0x6000) {
    uint8_t xormask = ((addr & 0x02) ? 0x80 : 0x20) | ((addr & 0x08) ? 0x08 : 0x02);
    return main_rom_ptr[addr - 0x6000] ^ xormask;
  }
  return m6809_read(s, addr);
}

unsigned char GAME_IRAM circusc::m6809_read(m6809_state *s, uint16_t addr) {
  if (addr >= 0x6000)
    return main_rom_ptr[addr - 0x6000];

  if ((addr & 0xE000) == 0x2000)          // RAM 0x2000-0x3FFF
    return memory[addr - 0x2000];

  if ((addr & 0xFC00) == 0x1000) {        // input (mirror 0x3FC)
    uint8_t keys = input->buttons_get();
    uint8_t val = 0xFF;
    switch (addr & 3) {
      case 0:   // SYSTEM: bit0 coin1, bit1 coin2, bit2 service, bit3 start1, bit4 start2
        if (keys & BUTTON_COIN)  val &= ~0x01;
        if (keys & BUTTON_START) val &= ~0x08;
        return val;
      case 1:   // P1: bit0 left, bit1 right (2-way), bit4 button1 (salto)
        if (keys & BUTTON_LEFT)  val &= ~0x01;
        if (keys & BUTTON_RIGHT) val &= ~0x02;
        if (keys & BUTTON_FIRE)  val &= ~0x10;
        return val;
      case 2:   // P2 (cocktail): non collegato
      case 3:   // DIPSW3 non popolato
        return 0xFF;
    }
  }

  if ((addr & 0xFC00) == 0x1400)          // DSW1 (coinage)
    return CIRCUSC_DSW1;

  if ((addr & 0xFC00) == 0x1800) {        // DSW2
    uint8_t dsw2 = CIRCUSC_DSW2;
    if (input->demoSoundsOff()) dsw2 |= CIRCUSC_DSW2_DEMO_SOUND_OFF;
    return dsw2;
  }

  return 0xFF;
}

void GAME_IRAM circusc::m6809_write(m6809_state *s, uint16_t addr, uint8_t val) {
  if ((addr & 0xE000) == 0x2000) {        // RAM 0x2000-0x3FFF
    memory[addr - 0x2000] = val;
    return;
  }

  if (addr < 0x0400) {                    // LS259 (mirror 0x3F8, A0-A2 = Q)
    uint8_t bval = val & 1;
    switch (addr & 7) {
      case 0: break;                      // flip screen: ignorato
      case 1:                             // INTST: IRQ mask vblank
        irq_mask = bval;
        if (!bval) main_cpu.irq_pending = 0;
        break;
      case 2: break;                      // MUT: non usato
      case 3: case 4: break;              // coin counter
      case 5: spritebank = bval; break;   // OBJ CHENG: banco sprite
    }
    return;
  }

  if (addr < 0x0800) return;              // watchdog

  if (addr < 0x0C00) {                    // soundlatch
    soundlatch = val;
    return;
  }

  if (addr < 0x1000) {                    // SOUND-ON: IRQ al Z80 (HOLD_LINE)
    snd_irq_pend = 1;
    return;
  }

  if ((addr & 0xFC00) == 0x1C00) {        // scroll (il dato scritto)
    scroll = val;
    return;
  }
}

// ============================================================
// Z80 audio
// ============================================================
unsigned char circusc::opZ80(unsigned short Addr) {
  if (Addr < 0x4000) return audio_rom_ptr[Addr];
  return 0xFF;
}

unsigned char circusc::rdZ80(unsigned short Addr) {
  if (Addr < 0x4000) return audio_rom_ptr[Addr];

  if ((Addr & 0xE000) == 0x4000)          // RAM 1KB (mirror su 0x4000-0x5FFF)
    return snd_ram[Addr & 0x3FF];

  if ((Addr & 0xE000) == 0x6000)          // soundlatch
    return soundlatch;

  if ((Addr & 0xE000) == 0x8000) {
    // sh_timer: MAME = (cicli_Z80 >> 9) & 0x1E, cioe' un contatore a
    // 4 bit su D1-D4 che avanza a 3.58MHz/1024 = 58.3 tick/frame.
    // snd_icnt avanza di 3 per iterazione (1875/frame): /32 = 58.6/frame
    // (stessa taratura del timer LS90 di rocnrope, validata su HW).
    return ((snd_icnt / 32) & 0x0F) << 1;
  }

  return 0xFF;
}

void circusc::wrZ80(unsigned short Addr, unsigned char Value) {
  if ((Addr & 0xE000) == 0x4000) {
    snd_ram[Addr & 0x3FF] = Value;
    return;
  }

  if ((Addr & 0xE000) == 0xA000) {
    // 0xA000-0xA07F (mirror 0x1F80): CS2-CS6 selezionati da offset&7
    switch (Addr & 7) {
      case 0: sn_latch = Value; break;      // CS2: latch dato SN
      case 1: sn[0].write(sn_latch); break; // CS3: scrittura SN #1
      case 2: sn[1].write(sn_latch); break; // CS4: scrittura SN #2
      case 3: dac_val = Value; break;       // CS5: DAC 8 bit
      case 4: break;                        // CS6: filtri RC (ignorati)
    }
  }
}

// ============================================================
// Frame: KONAMI-1 @1.536MHz + Z80 @3.58MHz interlacciati.
// Taratura come rocnrope (9 istr M6809 x 625 = 5625/frame ~ 25.3k cicli);
// Z80 a 3.58MHz = 2x rocnrope -> 8 step/iterazione.
// Il DAC viene campionato nel loop a 400 campioni/frame (= 24kHz) nel
// ring: il render audio fa solo pop (MAI steppare CPU nel render audio).
// ============================================================
// Main CPU idle loop (decrypted opcodes):
//   60ed LDX <$1a / LDD ,X / ASLA / LBCS $6203;  6203 JMP $60ed
// waits for the task queue slot [<$1a] to lose bit 7, ~70% of the main
// CPU's instructions. Only the vblank IRQ handler (after the slices) fills
// the queue: while PC is on the LDX and the slot is still empty, a slice
// is skipped. 9 instructions per slice over a 5 instruction loop: every
// alignment reaches the LDX within 5 slices.
static const uint16_t IDLE_PC = 0x60ED;
static const uint8_t IDLE_PTR_DP = 0x1A;
static const uint8_t IDLE_SLOT_EMPTY = 0x80;
static const int IDLE_PASS_INSTR = 5;
static const int IDLE_PASS_CYCLES = 21;   // one pass in the core's cycles (test only)

bool circusc::main_idle() {
  if (main_cpu.PC != IDLE_PC) {
    return false;
  }
  const uint16_t ptr = (main_cpu.DP << 8) | IDLE_PTR_DP;
  const uint16_t slot = (m6809_read(&main_cpu, ptr) << 8) | m6809_read(&main_cpu, ptr + 1);
  return m6809_read(&main_cpu, slot) & IDLE_SLOT_EMPTY;
}

void circusc::run_frame(void) {
  for (int i = 0; i < INST_PER_FRAME / 2; i++) {
    if (!main_idle()) {
      m6809_step(&main_cpu, 9);
    }

    StepZ80(&cpu[0]); StepZ80(&cpu[0]); StepZ80(&cpu[0]); StepZ80(&cpu[0]);
    StepZ80(&cpu[0]); StepZ80(&cpu[0]); StepZ80(&cpu[0]); StepZ80(&cpu[0]);
    snd_icnt += 3;   // timer 0x8000: 1875/frame, /32 = 58.6 tick/frame

    if (snd_irq_pend && (cpu[0].IFF & IFF_1)) {
      IntZ80(&cpu[0], INT_RST38);
      snd_irq_pend = 0;
    }

    // campionamento DAC: 400 campioni ogni 625 iterazioni = 24000/s
    dac_acc += 400;
    if (dac_acc >= 625) {
      dac_acc -= 625;
      uint16_t nxt = (dac_wr + 1) & (CC_DAC_RING - 1);
      if (nxt != dac_rd) {              // ring pieno: scarta
        dac_ring[dac_wr] = dac_val;
        dac_wr = nxt;
      }
    }
  }

  // vblank: IRQ al main se abilitato + snapshot del banco sprite attivo
  if (irq_mask)
    m6809_irq(&main_cpu);

  memcpy(spr_buf, &memory[CC_SPRITERAM + (spritebank ? 0x100 : 0)], 0x100);
}

// ============================================================
// Sprite (dal buffer vblank): 64 slot da 4 byte.
// MAME: code = sr[0] + 8*(sr[1]&0x20), color = sr[1]&0x0F, sx = sr[2]
// (X landscape), sy = sr[3] (Y landscape), flipx = bit6, flipy = bit7.
// Portrait (ricetta timeplt ROT90): gal_x = 239 - y, gal_y = x + 16
//   -> spr.x = 224 - sy (top del 16x16), spr.y = sx + 16.
// code > 255: bit alto in color_block (sprite_S.code e' a 8 bit).
// ============================================================
void circusc::prepare_frame(void) {
  active_sprites = 0;
  if (!game_started) return;

  for (int offs = 0; offs < 0x100 && active_sprites < 124; offs += 4) {
    unsigned short code = spr_buf[offs] + 8 * (spr_buf[offs + 1] & 0x20);
    if (code >= 384) continue;          // oltre la ROM sprite
    uint8_t attr = spr_buf[offs + 1];
    uint8_t sx   = spr_buf[offs + 2];
    uint8_t sy   = spr_buf[offs + 3];

    struct sprite_S spr;
    spr.x     = 224 - (int)sy;          // gal_x del bordo alto
    spr.y     = (int)sx + 16;           // gal_y
    spr.code  = code & 0xFF;
    spr.color_block = code >> 8;        // bit8 del codice (384 sprite)
    spr.color = attr & 0x0F;
    spr.flags = ((attr >> 6) & 1) |     // bit0 = flipx nativo (colonne)
                ((attr >> 6) & 2);      // bit1 = flipy nativo (righe)

    if (spr.x > -16 && spr.x < 224 && spr.y > -16 && spr.y < 304)
      sprite[active_sprites++] = spr;
  }
}

// blit trasposto stile rocnrope (dati landscape, una sola copia, flip a
// runtime) con la geometria ROT90: riga ROM r -> screen_x = spr_x + 15 - r,
// colonna ROM c -> screen_y = spr_y + c. pen trasparente = cmap 0.
void circusc::blit_sprite(short row, unsigned char s) {
  unsigned short code = sprite[s].code | (sprite[s].color_block << 8);
  const uint8_t (*spr)[8] = circusc_spritemap[code];
  const uint16_t *pal = circusc_colormap_sprites[sprite[s].color & 0x0F];

  uint8_t flipx = sprite[s].flags & 1;        // inverte le colonne (gal_y)
  uint8_t flipy = (sprite[s].flags >> 1) & 1; // inverte le righe (gal_x)

  int spr_x = sprite[s].x;
  int spr_y = sprite[s].y;
  int row_start = 8 * row;

  int y_begin = (spr_y > row_start) ? spr_y : row_start;
  int y_end   = ((spr_y + 15) < (row_start + 7)) ? (spr_y + 15) : (row_start + 7);
  if (y_begin > y_end) return;

  for (int r = 0; r < 16; r++) {
    int screen_x = spr_x + 15 - r;
    if (screen_x < 0 || screen_x >= 224) continue;
    const uint8_t *row_data = spr[flipy ? 15 - r : r];

    for (int screen_y = y_begin; screen_y <= y_end; screen_y++) {
      int c = screen_y - spr_y;
      int sc = flipx ? 15 - c : c;
      uint8_t px = (row_data[sc >> 1] >> ((sc & 1) << 2)) & 0x0F;
      if (px) {
        uint16_t col = pal[px];
        if (col) frame_buffer[(screen_y - row_start) * 224 + screen_x] = col;
      }
    }
  }
}

// ============================================================
// Tile: 8x8 4bpp pre-ruotati, blit con offset x arbitrario (per lo
// scroll) e clipping. pass 0 = tile con attr bit4=1 (categoria 1, sotto
// gli sprite), pass 1 = tile con attr bit4=0 (categoria 0, OPACHI sopra
// gli sprite). Ogni tile una sola volta.
// ============================================================
void circusc::blit_tile_x(unsigned short addr, short x, unsigned char pass) {
  uint8_t attr = memory[CC_COLORRAM + addr];
  const uint8_t category = (attr & 0x10) ? 1 : 0;
  if (category == pass) {
    return;   // pass 0: categoria 1, pass 1: categoria 0
  }

  unsigned short code = memory[CC_VIDEORAM + addr] + ((attr & 0x20) << 3);
  const uint8_t (*tile)[4] = circusc_tilemap[code];
  const uint16_t *pal = circusc_colormap_tiles[attr & 0x0F];

  // flip nativi: bit6=flipX (asse X MAME = gal_y) -> portrait flip_y;
  //              bit7=flipY (asse Y MAME = gal_x) -> portrait flip_x
  uint8_t flip_y = (attr >> 6) & 1;
  uint8_t flip_x = (attr >> 7) & 1;

  uint8_t c0 = (x < 0) ? -x : 0;
  uint8_t c1 = (x > 224 - 8) ? 224 - x : 8;

  unsigned short *ptr = frame_buffer + x;
  for (uint8_t r = 0; r < 8; r++, ptr += 224) {
    const uint8_t *trow = tile[flip_y ? 7 - r : r];

    // unclipped: 2 pixels per byte, low nibble first
    if (c0 == 0 && c1 == 8) {
      for (uint8_t i = 0; i < 4; i++) {
        const uint8_t b = trow[flip_x ? 3 - i : i];
        ptr[2 * i]     = pal[flip_x ? b >> 4 : b & 0x0F];
        ptr[2 * i + 1] = pal[flip_x ? b & 0x0F : b >> 4];
      }
      continue;
    }

    for (uint8_t c = c0; c < c1; c++) {
      uint8_t sc = flip_x ? 7 - c : c;
      ptr[c] = pal[(trow[sc >> 1] >> ((sc & 1) << 2)) & 0x0F];
    }
  }
}

// strip r: colonna MAME tx = r-2. Strip 2-11 (colonne 0-9) = HUD fisso;
// strip 12-33 scrollate. Un tile ty (0..31) appare a
// gal_x = ((232 - 8*ty + scroll) & 0xFF), wrap sul tilemap alto 256.
void circusc::render_tiles(short row, unsigned char pass) {
  uint8_t tx = row - 2;
  uint8_t scr = (tx >= 10) ? scroll : 0;

  for (uint8_t ty = 0; ty < 32; ty++) {
    short x = (232 - 8 * ty + scr) & 0xFF;
    if (x >= 224) x -= 256;
    if (x > -8)
      blit_tile_x((ty << 5) + tx, x, pass);
  }
}

void circusc::render_row(short row) {
  if (row < 2 || row >= 34) return;
  if (!game_started) return;

  // MAME: fill(0) + tile categoria 1 -> sprite -> tile categoria 0 opachi.
  // frame_buffer arriva azzerato (fill(0)); i tile categoria 0 sono opachi,
  // quindi coprono comunque tutto cio' che sta sotto.
  render_tiles(row, 0);

  for (unsigned char s = 0; s < active_sprites; s++)
    if ((sprite[s].y < 8 * (row + 1)) && ((sprite[s].y + 16) > 8 * row))
      blit_sprite(row, s);

  render_tiles(row, 1);
}
