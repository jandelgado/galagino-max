#ifndef ROADFIGHTER_H
#define ROADFIGHTER_H

#include "../machineBase.h"

#ifdef ENABLE_ROADFIGHTER

#include "../../cpus/m6809/m6809.h"
#include "roadfighter_palette.h"      // tile/sprite colormap (palette+clut bakate)
#include "roadfighter_logo.h"

// ============================================================================
// Road Fighter (Konami, 1984) — port SPINNERINO (board ESP32-P4).
// Driver MAME: konami/hyperspt.cpp (roadf_state). KONAMI-1/M6809 + Z80 audio.
//
// >>> FASE 2 = CPU + memory map + audio Z80 + run_frame <<<
//   La CPU M6809 esegue il gioco (raw=dati, decrypted=opcode KONAMI-1).
//   Video (tilemap+sprite) e mix audio nelle FASI 3-4. render_row ancora nero.
//
// Memory map main (roadf_state, common_map):
//   $1000-$10BF sprite RAM   $10C0-$10FF scroll (per-row)
//   $1400 watchdog (no-op)   $1480-$1487 LS259 (Q0 flip, Q1 sndIRQ, Q3/Q4 coin, Q7 irq_mask)
//   $1500 sound latch        $1600 DSW2
//   $1680 system in          $1681 P1   $1682 P2   $1683 DSW1
//   $2000-$27FF VRAM         $2800-$2FFF CRAM
//   $3000-$37FF work RAM     $3800-$3FFF NVRAM      $4000-$FFFF ROM
//
// IRQ: IRQ0 (non NMI) a vblank quando irq_mask=1; ack su irq_mask=0.
// ============================================================================

#define ROADF_SCREEN_W 256
#define ROADF_SCREEN_H 224

// Offset nel buffer condiviso memory[] (16 KB, copre CPU $0000-$3FFF)
#define ROADF_SPRRAM_OFF  0x1000   // 0x1000-0x10BF (192 byte)
#define ROADF_SCROLL_OFF  0x10C0   // 0x10C0-0x10FF (64 byte)
#define ROADF_VRAM_OFF    0x2000   // 0x2000-0x27FF (2 KB)
#define ROADF_CRAM_OFF    0x2800   // 0x2800-0x2FFF (2 KB)

// DIP di default da MAME INPUT_PORTS(roadf):
//   DSW2 = 0x2D: Continue=No, Opponents=Normal, Speed=Fast, Fuel=Normal,
//                Cabinet=UPRIGHT (bit6=0), Demo Sounds=On (bit7=0).
//   DSW1 = 0xFF: KONAMI coinage 1C/1C (nibble alti/bassi = 0xF).
#define ROADF_DSW1  0xFF
#define ROADF_DSW2  0x2D

class roadfighter : public machineBase {
public:
  roadfighter();
  ~roadfighter();

  void init(Input *input, unsigned short *framebuffer,
            sprite_S *spritebuffer, unsigned char *memorybuffer) override;
  void reset() override;
  void start(void) override;

  signed char machineType()      override { return MCH_ROADFIGHTER; }

  // Audio Z80 (sound CPU)
  unsigned char rdZ80(unsigned short Addr) override;
  void          wrZ80(unsigned short Addr, unsigned char Value) override;
  unsigned char opZ80(unsigned short Addr) override;
  unsigned char inZ80(unsigned short Port) override;
  void          outZ80(unsigned short Port, unsigned char Value) override;

  // M6809 main CPU bus (chiamate dalle callback C globali)
  //uint8_t main_read(uint16_t addr);
  //void    main_write(uint16_t addr, uint8_t val);
  //uint8_t main_read_opcode(uint16_t addr);

  unsigned char m6809_read(m6809_state *s, uint16_t addr) override;
  void m6809_write(m6809_state *s, uint16_t addr, uint8_t val) override;
  unsigned char m6809_read_opcode(m6809_state *s, uint16_t addr) override;

  void run_frame(void)      override;
  void prepare_frame(void)  override;
  void render_row(short row) override;

  static RomData<unsigned short, COMPRESSED> &logo(void) { return roadfighter_logo; }

  int roadf_dac_sample() const { return dac_sample; }

private:
  m6809_state main_cpu;

  // Audio Z80 state
  unsigned char snd_ram[0x1000];   // $4000-$4FFF (4 KB)
  unsigned char sound_latch;
  unsigned char sn_latch;
  unsigned char sn_latch_reg;
  unsigned long snd_icnt;
  int           dac_sample;

  // Main I/O state
  unsigned short dbg_pc;            // PC M6809 catturato per overlay debug
  unsigned char irq_mask;
  unsigned char flip_screen;
  unsigned char coin_latch, coin_hold;
  unsigned char start_latch, start_hold;
  // Acceleratore a toggle su FIRE: ogni tap (premi+lascia) alterna le marce.
  //   0 = marcia LENTA,  1 = marcia VELOCE   (sempre in accelerazione, niente folle)
  unsigned char gear_state;
  unsigned char prev_fire_btn;    // edge detection del tap FIRE (1 volta/frame)

  void sn76489_write(unsigned char data);
  unsigned char input_system();
  unsigned char input_p1();

  // Video snapshot (tearing-free): catturati in prepare_frame
  unsigned char vram_snap[0x800];
  unsigned char cram_snap[0x800];
  unsigned char scroll_snap[0x40];
  unsigned char spr_snap[0xC0];
  void blit_sprite_strip(short row, unsigned char s);

  // Cached: hot path reads these per access; data() checks the cache on every call.
  const unsigned char *main_raw_ptr = nullptr;
  const unsigned char *main_decrypted_ptr = nullptr;
  const unsigned char *audio_rom_ptr = nullptr;
};

#endif // ENABLE_ROADFIGHTER
#endif // ROADFIGHTER_H
