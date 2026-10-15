#include "fantasy.h"
#include "fantasy_rom.h"
#include "fantasy_gfx.h"
#include "fantasy_proms.h"
#include "fantasy_sound_rom.h"
#include "fantasy_samples.h"

// Screen is rotated: an output line is one native x column, walked
// along native y. Tiles are stored pre-rotated like that, 8 words per
// tile, word q = native column q (mask 0x80 >> q), pixel c = 2 bit pen
// of native row 7 - c at bits 2c. One load then feeds 8 pixels.
// In MAME's planar decoder plane 0 (hi) is the most significant pen bit.
// Keeping the ROM order as bit 0 swapped palette pens 1 and 2 (for
// example blue sea became green and cyan clouds became white).
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

fantasy::fantasy()
  : fantasy_gfx(fantasy_gfx_blob),
    fantasy_rom(fantasy_rom_blob),
    fantasy_samples(fantasy_samples_blob),
    fantasy_sound_rom(fantasy_sound_rom_blob),
    sound(*this, Snk6502Sound::FANTASY) { memset(&m_cpu, 0, sizeof(m_cpu)); }

static_assert(FANTASY_SAMPLE_COUNT == 12, "Unexpected Fantasy sample count");

uint8_t fantasy::soundRom(uint16_t addr) {
  return addr < fantasy_sound_rom.size() ? fantasy_sound_rom[addr] : 0xff;
}

const signed char *fantasy::sample(uint8_t index) {
  if(index < 2 || index - 2 >= FANTASY_SAMPLE_COUNT) return nullptr;
  index -= 2;
  return (const signed char *)(fantasy_samples.data() + fantasy_sample_offsets[index]);
}

uint32_t fantasy::sampleLength(uint8_t index) {
  return index >= 2 && index - 2 < FANTASY_SAMPLE_COUNT ? fantasy_sample_lengths[index - 2] : 0;
}

uint8_t fantasy::sampleDivider(uint8_t index) {
  return index >= 2 && index - 2 < FANTASY_SAMPLE_COUNT ? 4 : 1;
}

uint16_t fantasy::pen(unsigned char p) const {
  unsigned char r=0x21*(p&1)+0x47*((p>>1)&1)+0x97*((p>>2)&1);
  unsigned char g=0x21*((p>>3)&1)+0x47*((p>>4)&1)+0x97*((p>>5)&1);
  unsigned char b=0x52*((p>>6)&1)+0xad*((p>>7)&1);
  uint16_t rgb=((r&0xf8)<<8)|((g&0xfc)<<3)|(b>>3);
  return (rgb>>8)|(rgb<<8);
}

void fantasy::start() {
  fg_ram=memory+0x400; bg_ram=memory+0x800; color_ram=memory+0xc00; char_ram=memory+0x1000;
  for(int i=0;i<64;i++) palette[i]=pen(fantasy_proms[i]);
  m_cpu.read=main_read; m_cpu.write=main_write; m_cpu.fetch=nullptr; m_cpu.user=this;
  rom_ptr = fantasy_rom.data();

  // start() runs twice: decode the ROM tiles once
  if(!bg_tiles) {
    const unsigned char *gfx = fantasy_gfx.data();
    bg_tiles = Arena::alloc<uint16_t>(512 * 8);
    fg_tiles = Arena::alloc<uint16_t>(256 * 8);
    for(int code = 0; code < 512; code++) {
      decode_tile(bg_tiles + 8 * code, gfx + 8 * code, gfx + 0x1000 + 8 * code);
    }
  }
  memset((void *)fg_dirty, 1, sizeof(fg_dirty));
  reset();
}

void fantasy::reset() {
  machineBase::reset();
  fg_ram=memory+0x400; bg_ram=memory+0x800; color_ram=memory+0xc00; char_ram=memory+0x1000;
  scroll_x=scroll_y=backcolor=flip_screen=0; charbank=1; coin_down=false;
  music_muted[0]=music_muted[1]=music_muted[2]=true;
  speech_cmd=speech_data_bytes=0; speech_addr=0;
  if(m_cpu.read) m6502_reset(&m_cpu);
}

uint8_t fantasy::main_read(m6502_t *cpu, uint16_t a) {
  fantasy *s=static_cast<fantasy *>(cpu->user);
  if(a<0x2000) return s->memory[a];
  if(a>=0x3000 && a<0xc000) return s->rom_ptr[a-0x3000];
  if(a>=0xf000) return s->rom_ptr[0x5000+(a&0x0fff)];
  unsigned char k=s->input->buttons_get();
  if(a==0x2104){
    unsigned char v=0;
    if(k&BUTTON_DOWN)v|=0x10; if(k&BUTTON_UP)v|=0x20;
    if(k&BUTTON_RIGHT)v|=0x40; if(k&BUTTON_LEFT)v|=0x80;
    return v;
  }
  if(a==0x2105) return 0;
  if(a==0x2106) return FANTASY_DSW;
  if(a==0x2107) return ((k&BUTTON_COIN)?2:0)|((k&BUTTON_START)?0x80:0);
  return 0xff;
}

void fantasy::set_music_level(unsigned char channel, bool enabled) {
  if(enabled){
    if(music_muted[channel]) soundregs[channel<2 ? 8+channel : 11]++;
    music_muted[channel]=false;
  }else music_muted[channel]=true;
}

void fantasy::speech_write(unsigned char v) {
  if((v&0x30)!=0x30) return;
  static const unsigned long table[12]={
    0x04000,0x04297,0x044b6,0x04682,0x04927,0x04be0,
    0x04cc2,0x04e36,0x05000,0x05163,0x052c9,0x053fd
  };
  unsigned char data=v&0x0f;
  if(speech_cmd==2){
    speech_addr|=((unsigned long)data)<<(speech_data_bytes++*4);
    if(speech_data_bytes==5)speech_cmd=0;
  }else if(speech_cmd==4 || speech_cmd==6 || speech_cmd==8){
    speech_cmd=0;
  }else if(data==2){
    speech_cmd=2;speech_addr=0;speech_data_bytes=0;
  }else if(data==4 || data==6 || data==8){
    speech_cmd=data;
  }else if(data==12 && speech_data_bytes==5){
    for(unsigned char i=0;i<12;i++) if(table[i]==speech_addr){soundregs[3]=i;soundregs[4]++;break;}
  }else if(data==10){
    soundregs[3]=0xff;soundregs[4]++;
  }
}

void fantasy::main_write(m6502_t *cpu, uint16_t a, uint8_t v) {
  fantasy *s=static_cast<fantasy *>(cpu->user);
  if(a<0x2000){
    s->memory[a]=v;
    if(a>=0x1000){
      s->fg_dirty[(a>>3)&0xff]=1;
    }
    return;
  }
  if(a>=0x2100 && a<=0x2102){
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
  if(a==0x2400){s->speech_write(v);return;}
}

void fantasy::run_frame() {
  unsigned char k=input->buttons_get(); bool coin=(k&BUTTON_COIN)!=0;
  if(coin&&!coin_down)m_cpu.nmi=1; coin_down=coin;
  m6502_exec(&m_cpu,23520);
  m_cpu.irq=1;m6502_exec(&m_cpu,8);m_cpu.irq=0;
  if(!game_started)game_started=1;
}

// Re-decode RAM chars the CPU wrote since the last frame. Flag cleared
// before the decode: a write during it sets it again for the next frame.
void fantasy::prepare_frame() {
  for(int code = 0; code < 256; code++) {
    if(!fg_dirty[code]) {
      continue;
    }
    fg_dirty[code] = 0;
    decode_tile(fg_tiles + 8 * code, char_ram + 8 * code, char_ram + 0x800 + 8 * code);
  }
}

void fantasy::render_row(short strip) {
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
