#include "vanguard.h"
#include "vanguard_rom.h"
#include "vanguard_gfx.h"
#include "vanguard_proms.h"
#include "vanguard_sound_rom.h"
#include "vanguard_samples.h"

vanguard::vanguard()
  : vanguard_gfx(vanguard_gfx_blob),
    vanguard_rom(vanguard_rom_blob),
    vanguard_samples(vanguard_samples_blob),
    vanguard_sound_rom(vanguard_sound_rom_blob) { memset(&m_cpu, 0, sizeof(m_cpu)); }

static_assert(VANGUARD_SAMPLE_COUNT == 18, "Unexpected Vanguard sample count");

static const uint16_t PLANE_SIZE = 0x800; // 2 bitplanes, 256 codes x 8 rows each

// set bit (row, plane) of the 8 column words of one code row byte
static void set_cols(uint16_t *cols, uint16_t offset, uint8_t value) {
  uint16_t *w = cols + (((offset & (PLANE_SIZE - 1)) >> 3) << 3);
  const uint16_t mask = 1 << (((offset & 7) << 1) | (offset / PLANE_SIZE));
  for (int c = 0; c < 8; c++) {
    if ((value << c) & 0x80) {
      w[c] |= mask;
    } else {
      w[c] &= ~mask;
    }
  }
}

unsigned char vanguard::vanguardSoundRom(unsigned short addr) {
  return vanguard_sound_rom[addr & 0x0fff];
}

const signed char *vanguard::vanguardSample(unsigned char index) {
  return index < VANGUARD_SAMPLE_COUNT ?
    reinterpret_cast<const signed char *>(vanguard_samples.data() + vanguard_sample_offsets[index]) : nullptr;
}

unsigned long vanguard::vanguardSampleLength(unsigned char index) {
  return index < VANGUARD_SAMPLE_COUNT ? vanguard_sample_lengths[index] : 0;
}

unsigned char vanguard::vanguardSampleDivider(unsigned char index) {
  return index < VANGUARD_SAMPLE_COUNT ? vanguard_sample_dividers[index] : 1;
}

uint16_t vanguard::pen(unsigned char p) const {
  unsigned char r = 0x21*(p&1) + 0x47*((p>>1)&1) + 0x97*((p>>2)&1);
  unsigned char g = 0x21*((p>>3)&1) + 0x47*((p>>4)&1) + 0x97*((p>>5)&1);
  unsigned char b = 0x52*((p>>6)&1) + 0xad*((p>>7)&1);
  uint16_t rgb = ((r&0xf8)<<8) | ((g&0xfc)<<3) | (b>>3);
  return (rgb>>8) | (rgb<<8);
}

void vanguard::start() {
  work_ram=memory; char_ram=memory+0x1000;
  for (int i=0;i<64;i++) palette[i]=pen(vanguard_proms[i]);
  m_cpu.read=main_read; m_cpu.write=main_write; m_cpu.fetch=nullptr; m_cpu.user=this;
  rom_ptr = vanguard_rom.data();

  if (!bg_cols) {
    bg_cols = Arena::alloc<uint16_t>(COLS_WORDS);
    fg_cols = Arena::alloc<uint16_t>(COLS_WORDS);
    const unsigned char *gfx = vanguard_gfx.data();
    for (uint16_t i = 0; i < 2 * PLANE_SIZE; i++) {
      set_cols(bg_cols, i, gfx[i]);
    }
  }
  reset();
}

void vanguard::reset() {
  machineBase::reset();
  work_ram=memory; char_ram=memory+0x1000;
  scroll_x=scroll_y=backcolor=flip_screen=0; charbank=1; fire_direction=0; coin_down=false;
  music0_muted=music1_muted=true;
  speech_cmd=speech_data_bytes=0; speech_addr=0;
  if (m_cpu.read) m6502_reset(&m_cpu);

  // char RAM was just cleared
  if (fg_cols) {
    memset(fg_cols, 0, COLS_WORDS * sizeof(uint16_t));
  }
}

void vanguard::char_write(uint16_t offset, uint8_t value) {
  if (char_ram[offset] == value) {
    return;
  }
  char_ram[offset] = value;
  set_cols(fg_cols, offset, value);
}

uint8_t vanguard::main_read(m6502_t *cpu, uint16_t a) {
  vanguard *s=static_cast<vanguard*>(cpu->user);
  if (a<0x2000) return s->memory[a];
  if (a>=0x4000 && a<0xc000) return s->rom_ptr[a-0x4000];
  if (a>=0xf000) return s->rom_ptr[0x4000+(a&0xfff)];
  unsigned char k=s->input->buttons_get();
  if (a==0x3104) {
    unsigned char v=0;
    if(k&BUTTON_DOWN)v|=0x10; if(k&BUTTON_UP)v|=0x20;
    if(k&BUTTON_RIGHT)v|=0x40; if(k&BUTTON_LEFT)v|=0x80;
    // The original cabinet has four directional fire buttons.  Asserting
    // them together makes the game prioritise fire-down, so the single
    // Galagino FIRE button cycles through the four independent lines.
    if(k&BUTTON_FIRE) v|=(1u << s->fire_direction);
    return v;
  }
  if (a==0x3105) return 0;
  if (a==0x3106) return VANGUARD_DSW;
  if (a==0x3107) return ((k&BUTTON_COIN)?2:0)|((s->music0_muted)?0x10:0)|((k&BUTTON_START)?0x80:0);
  return 0xff;
}

void vanguard::main_write(m6502_t *cpu, uint16_t a, uint8_t v) {
  vanguard *s=static_cast<vanguard*>(cpu->user);
  if(a>=0x1000&&a<0x2000){s->char_write(a-0x1000,v);return;}
  if(a<0x2000){s->memory[a]=v;return;}
  if(a>=0x3100&&a<=0x3102){
    s->soundregs[a-0x3100]=v;
    s->soundregs[5+(a-0x3100)]++; // preserve write strobes for the asynchronous audio task
    if(a==0x3100){
      if(v&0x08)s->music0_muted=true;
      if(v&0x10){
        // The original unmute_channel() restarts the 256-byte tune only when
        // the channel was muted. Repeated unmute writes must not rewind it.
        bool restart=s->music0_muted;
        s->music0_muted=false;
        if(restart)s->soundregs[8]++;
      }
    }else if(a==0x3101){
      // Channel 1 uses bit 3 as a level. Preserve an unmute event even if a
      // later mute write arrives before the asynchronous audio task runs.
      if(v&0x08){
        if(s->music1_muted)s->soundregs[9]++;
        s->music1_muted=false;
      }else s->music1_muted=true;
    }
    return;
  }
  if(a==0x3103){s->backcolor=v&7;s->charbank=(~v>>3)&1;s->flip_screen=v&0x80;return;}
  if(a==0x3200){s->scroll_x=v;return;} if(a==0x3300){s->scroll_y=v;return;}
  if(a==0x3400 && (v&0x30)==0x30) {
    static const unsigned long speech_table[16] = {
      0x04000,0x04325,0x044a2,0x045b7,0x046ee,0x04838,0x04984,0x04b01,
      0x04c38,0x04de6,0x04f43,0x05048,0x05160,0x05289,0x0539e,0x054ce
    };
    unsigned char data=v&0x0f;
    if(s->speech_cmd==2) { // HD38880 ADSET: five address nibbles, least significant first
      s->speech_addr|=((unsigned long)data)<<(s->speech_data_bytes++*4);
      if(s->speech_data_bytes==5)s->speech_cmd=0;
    } else if(s->speech_cmd==4 || s->speech_cmd==6 || s->speech_cmd==8) {
      s->speech_cmd=0; // INT1, INT2 and SYSPD each consume one parameter nibble
    } else if(data==2) {
      s->speech_cmd=2;s->speech_addr=0;s->speech_data_bytes=0;
    } else if(data==4 || data==6 || data==8) {
      s->speech_cmd=data;
    } else if(data==12 && s->speech_data_bytes==5) { // START
      for(unsigned char i=0;i<16;i++)if(speech_table[i]==s->speech_addr){
        s->soundregs[3]=i;s->soundregs[4]++;break;
      }
    } else if(data==10) { // STOP
      s->soundregs[3]=0xff;s->soundregs[4]++;
    }
    return;
  }
}

void vanguard::run_frame() {
  unsigned char k=input->buttons_get(); bool coin=(k&BUTTON_COIN)!=0;
  if(input->fire_raw()) fire_direction=(fire_direction+1)&3;
  else fire_direction=0;
  if(coin&&!coin_down)m_cpu.nmi=1; coin_down=coin;
  m6502_exec(&m_cpu,23520); // 11.289 MHz / 8 / 60 Hz

  vblank.publish([this](VideoState &v) {
    memcpy(v.tile_ram, memory + TILE_RAM_OFFSET, TILE_RAM_SIZE);
    v.scroll_x = scroll_x;
    v.scroll_y = scroll_y;
    v.backcolor = backcolor;
    v.flip_screen = flip_screen;
  });

  m_cpu.irq=1; m6502_exec(&m_cpu,8); m_cpu.irq=0;
  if(!game_started)game_started=1;
}

void vanguard::prepare_frame() {
  vblank.read(video);
}

// One output line = one native column sx, walked along native y (sy).
// STEP is the frame buffer direction for increasing sy (-1 unflipped: sy
// runs right to left), a template constant so the loops stay in registers.
//
// Two passes over whole tiles, one pre-rotated column word per tile (see
// bg_cols): bg (scrolled, opaque through a 4-entry pen table), then fg
// (fixed, pen 0 transparent, empty tiles skipped).
template <int STEP>
static void vanguard_line(uint16_t *p, int sx, int scroll_x, int scroll_y, uint16_t back,
                          const uint16_t *pal, const uint16_t *bg_cols, const uint16_t *fg_cols,
                          const unsigned char *bg, const unsigned char *fg, const unsigned char *col) {
  uint16_t *const start = p; // pixel for sy == 0

  // background: tile runs along by = sy + scroll_y, wrapping at 256
  const int bx = (sx + scroll_x) & 255;
  const unsigned char *bgcode = bg + (bx >> 3);
  const unsigned char *bgcolor = col + (bx >> 3);
  const uint16_t *bgcols = bg_cols + (bx & 7);
  int by = scroll_y;
  for (int sy = 0; sy < 224;) {
    const int ti = (by >> 3) << 5;
    const uint16_t *bpal = pal + 32 + ((bgcolor[ti] >> 3) & 7) * 4;
    const uint16_t pens[4] = { back, bpal[1], bpal[2], bpal[3] };
    const int r = by & 7;
    int n = 8 - r;
    if (n > 224 - sy) {
      n = 224 - sy;
    }
    sy += n;
    by = (by + n) & 255;

    uint32_t w = bgcols[bgcode[ti] << 3] >> (r << 1);
    for (; n; n--, w >>= 2, p += STEP) {
      *p = pens[w & 3];
    }
  }

  // foreground: 28 fixed tiles, 8 pixels each
  const unsigned char *fgcode = fg + (sx >> 3);
  const unsigned char *fgcolor = col + (sx >> 3);
  const uint16_t *fgcols = fg_cols + (sx & 7);
  p = start;
  for (int ti = 0; ti < 28 << 5; ti += 32, p += 8 * STEP) {
    uint32_t w = fgcols[fgcode[ti] << 3];
    if (!w) {
      continue;
    }

    const uint16_t *fpal = pal + (fgcolor[ti] & 7) * 4;
    for (uint16_t *q = p; w; w >>= 2, q += STEP) {
      if (w & 3) {
        *q = fpal[w & 3];
      }
    }
  }
}

void vanguard::render_row(short strip) {
  // Vanguard is ROT90 in MAME: each output line is one native column.
  const uint16_t back = palette[32 + video.backcolor * 4];
  const unsigned char *fg = video.tile_ram;
  const unsigned char *bg = fg + 0x400, *col = fg + 0x800;
  for (int oy = 0; oy < 8; oy++) {
    const int py = strip * 8 + oy - 16;
    if (py < 0 || py >= 256) {
      continue;
    }

    uint16_t *line = frame_buffer + oy * 224;
    if (video.flip_screen) {
      vanguard_line<1>(line, 255 - py, video.scroll_x, video.scroll_y, back,
                       palette, bg_cols, fg_cols, bg, fg, col);
    } else {
      vanguard_line<-1>(line + 223, py, video.scroll_x, video.scroll_y, back,
                        palette, bg_cols, fg_cols, bg, fg, col);
    }
  }
}
