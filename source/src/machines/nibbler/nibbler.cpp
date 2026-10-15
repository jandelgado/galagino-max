#include "nibbler.h"
#include "nibbler_rom.h"
#include "nibbler_gfx.h"
#include "nibbler_proms.h"
#include "nibbler_sound_rom.h"

// Screen is rotated: an output line is one native x column, walked
// along native y. Tiles are stored pre-rotated like that, 8 words per
// tile, word q = native column q (mask 0x80 >> q), pixel c = 2 bit pen
// of native row 7 - c at bits 2c. One load then feeds 8 pixels.
static void decode_tile(uint16_t *dst, const unsigned char *hi, const unsigned char *lo) {
  for(int q = 0; q < 8; q++) {
    uint16_t w = 0;
    for(int c = 0; c < 8; c++) {
      const int n = 7 - c;
      w |= (((hi[n] >> (7 - q)) & 1) << 1 | ((lo[n] >> (7 - q)) & 1)) << (2 * c);
    }
    dst[q] = w;
  }
}

// flipped screen walks native rows the other way: pixel c <-> 7 - c
static inline uint16_t reverse_pens(uint16_t w) {
  w = (w >> 8) | (w << 8);
  w = ((w & 0xf0f0) >> 4) | ((w & 0x0f0f) << 4);
  return ((w & 0xcccc) >> 2) | ((w & 0x3333) << 2);
}

nibbler::nibbler()
  : nibbler_gfx(nibbler_gfx_blob),
    nibbler_rom(nibbler_rom_blob),
    nibbler_sound_rom(nibbler_sound_rom_blob),
    sound(*this, Snk6502Sound::FANTASY) { memset(&m_cpu,0,sizeof(m_cpu)); }

uint8_t nibbler::soundRom(uint16_t addr) {
  return addr < nibbler_sound_rom.size() ? nibbler_sound_rom[addr] : 0xff;
}

uint16_t nibbler::pen(unsigned char p) const {
  unsigned char r=0x21*(p&1)+0x47*((p>>1)&1)+0x97*((p>>2)&1);
  unsigned char g=0x21*((p>>3)&1)+0x47*((p>>4)&1)+0x97*((p>>5)&1);
  unsigned char b=0x52*((p>>6)&1)+0xad*((p>>7)&1);
  uint16_t rgb=((r&0xf8)<<8)|((g&0xfc)<<3)|(b>>3);
  return (rgb>>8)|(rgb<<8);
}

void nibbler::start() {
  fg_ram=memory+0x400;bg_ram=memory+0x800;color_ram=memory+0xc00;char_ram=memory+0x1000;
  for(int i=0;i<64;i++)palette[i]=pen(nibbler_proms[i]);
  m_cpu.read=main_read;m_cpu.write=main_write;m_cpu.fetch=nullptr;m_cpu.user=this;
  rom_ptr = nibbler_rom.data();

  // start() runs twice: decode the ROM tiles once
  if(!bg_tiles) {
    const unsigned char *gfx = nibbler_gfx.data();
    bg_tiles = Arena::alloc<uint16_t>(512 * 8);
    fg_tiles = Arena::alloc<uint16_t>(256 * 8);
    for(int code = 0; code < 512; code++) {
      decode_tile(bg_tiles + 8 * code, gfx + 8 * code, gfx + 0x1000 + 8 * code);
    }
  }
  memset((void *)fg_dirty, 1, sizeof(fg_dirty));
  reset();
}

void nibbler::reset() {
  machineBase::reset();
  fg_ram=memory+0x400;bg_ram=memory+0x800;color_ram=memory+0xc00;char_ram=memory+0x1000;
  scroll_x=scroll_y=backcolor=flip_screen=0;charbank=1;coin_down=false;
  music_muted[0]=music_muted[1]=music_muted[2]=true;
  if(m_cpu.read)m6502_reset(&m_cpu);
}

uint8_t nibbler::main_read(m6502_t *cpu,uint16_t a) {
  nibbler *s=static_cast<nibbler *>(cpu->user);
  if(a<0x2000)return s->memory[a];
  if(a>=0x3000&&a<0xc000)return s->rom_ptr[a-0x3000];
  if(a>=0xf000)return s->rom_ptr[0x5000+(a&0x0fff)];
  unsigned char k=s->input->buttons_get();
  if(a==0x2104){
    unsigned char v=0;
    if(k&BUTTON_DOWN)v|=0x10;if(k&BUTTON_UP)v|=0x20;
    if(k&BUTTON_RIGHT)v|=0x40;if(k&BUTTON_LEFT)v|=0x80;
    return v;
  }
  if(a==0x2105)return 0;
  if(a==0x2106)return NIBBLER_DSW;
  if(a==0x2107)return ((k&BUTTON_COIN)?2:0)|((k&BUTTON_START)?0x80:0);
  return 0xff;
}

void nibbler::set_music_level(unsigned char channel,bool enabled) {
  if(enabled){
    if(music_muted[channel])soundregs[channel<2?8+channel:11]++;
    music_muted[channel]=false;
  }else music_muted[channel]=true;
}

void nibbler::main_write(m6502_t *cpu,uint16_t a,uint8_t v) {
  nibbler *s=static_cast<nibbler *>(cpu->user);
  if(a<0x2000){
    s->memory[a]=v;
    if(a>=0x1000){
      s->fg_dirty[(a>>3)&0xff]=1;
    }
    return;
  }
  if(a>=0x2100&&a<=0x2102){
    unsigned char port=a-0x2100;
    s->soundregs[port]=v;s->soundregs[5+port]++;
    if(port==0){s->set_music_level(0,v&0x08);s->set_music_level(2,v&0x10);}
    else if(port==1)s->set_music_level(1,v&0x08);
    return;
  }
  if(a==0x2103){
    s->soundregs[10]=v;s->soundregs[7]++;
    s->backcolor=v&7;s->charbank=(~v>>3)&1;s->flip_screen=v&0x80;
    return;
  }
  if(a==0x2200){s->scroll_x=v;return;}
  if(a==0x2300){s->scroll_y=v;return;}
}

void nibbler::run_frame() {
  unsigned char k=input->buttons_get();bool coin=(k&BUTTON_COIN)!=0;
  if(coin&&!coin_down)m_cpu.nmi=1;coin_down=coin;
  m6502_exec(&m_cpu,23520);
  m_cpu.irq=1;m6502_exec(&m_cpu,8);m_cpu.irq=0;
  if(!game_started)game_started=1;
}

// Re-decode RAM chars the CPU wrote since the last frame. Flag cleared
// before the decode: a write during it sets it again for the next frame.
void nibbler::prepare_frame() {
  for(int code = 0; code < 256; code++) {
    if(!fg_dirty[code]) {
      continue;
    }
    fg_dirty[code] = 0;
    decode_tile(fg_tiles + 8 * code, char_ram + 8 * code, char_ram + 0x800 + 8 * code);
  }
}

void nibbler::render_row(short strip) {
  const int py0 = strip * 8 - 16;
  if(py0 < 0 || py0 >= 256) {
    return;
  }

  const bool flip = flip_screen != 0;
  const unsigned short bank = charbank << 8;
  const unsigned short back = palette[32 + backcolor * 4];

  // bg, per line: column bx, scrolled native rows cross tile rows
  // anywhere, so spans of up to 8 pixels per tile
  for(int oy = 0; oy < 8; oy++) {
    const int sx = flip ? 255 - (py0 + oy) : py0 + oy;
    const int bx = (sx + scroll_x) & 255;
    const unsigned char bcol = bx >> 3, q = bx & 7;
    unsigned short *dst = frame_buffer + oy * 224;
    int by = ((flip ? 0 : 223) + scroll_y) & 255;
    int ox = 0;
    while(ox < 224) {
      const unsigned short ti = ((by >> 3) << 5) + bcol;
      uint16_t w = bg_tiles[8 * (bg_ram[ti] + bank) + q];
      if(flip) {
        w = reverse_pens(w);
      }
      const unsigned short *pal = palette + 32 + ((color_ram[ti] >> 3) & 7) * 4;
      const int c = flip ? by & 7 : 7 - (by & 7);
      int n = 8 - c;
      if(n > 224 - ox) {
        n = 224 - ox;
      }
      w >>= 2 * c;
      for(int i = 0; i < n; i++, w >>= 2) {
        dst[ox++] = (w & 3) ? pal[w & 3] : back;
      }
      by = (flip ? by + n : by - n) & 255;
    }
  }

  // fg, per tile: tile rows line up with the 8 pixel chunks, pen 0
  // shows the bg
  const int fcol = (flip ? 255 - py0 : py0) >> 3;
  for(int k = 0; k < 28; k++) {
    const unsigned short ti = ((flip ? k : 27 - k) << 5) + fcol;
    const uint16_t *tile = fg_tiles + 8 * fg_ram[ti];
    const unsigned short *pal = palette + (color_ram[ti] & 7) * 4;
    unsigned short *dst = frame_buffer + 8 * k;
    for(int oy = 0; oy < 8; oy++, dst += 224) {
      uint16_t w = flip ? reverse_pens(tile[7 - oy]) : tile[oy];
      for(int c = 0; w; c++, w >>= 2) {
        if(w & 3) {
          dst[c] = pal[w & 3];
        }
      }
    }
  }
}
