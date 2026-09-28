#include "starforce.h"
#include "starforce_logo.h"
#include "starforce_dipswitches.h"
#include "starforce_bg1_tiles.h"
#include "starforce_bg2_tiles.h"
#include "starforce_bg3_tiles.h"
#include "starforce_fg_tiles.h"
#include "starforce_sprites.h"
#include "starforce_main_cpu_rom.h"
#include "starforce_sub_cpu_rom.h"

starforce::starforce() : starforce(Roms{
	starforce_main_cpu_rom_blob, starforce_sub_cpu_rom_blob, starforce_fg_tilemap_blob,
	starforce_bg1_tilemap_blob, starforce_bg2_tilemap_blob, starforce_bg3_tilemap_blob,
	starforce_sprites_16x16_blob, starforce_sprites_32x32_blob,
}) { }

starforce::starforce(const Roms &r)
	: main_cpu_rom(r.main_cpu), sub_cpu_rom(r.sub_cpu), fg_tiles(r.fg),
	  bg1_tiles(r.bg1), bg2_tiles(r.bg2), bg3_tiles(r.bg3),
	  sprites_16x16(r.sprites_16x16), sprites_32x32(r.sprites_32x32) {
	// decompress largest first: heap is freshest right after create(), so
	// big blocks get placed before smaller ones fragment it
	spr16_ptr = sprites_16x16.data();
	spr32_ptr = sprites_32x32.data();
	rom_main_ptr = main_cpu_rom.data();
	bg_ptr[0] = bg1_tiles.data();
	bg_ptr[1] = bg2_tiles.data();
	bg_ptr[2] = bg3_tiles.data();
	fg_ptr = fg_tiles.data();
	rom_sub_ptr = sub_cpu_rom.data();
}

uint8_t starforce::dsw1() {
  return STARFORCE_DSW1 | (input->demoSoundsOff() ? STARFORCE_DSW2_DEMO_SOUND_OFF : STARFORCE_DSW2_DEMO_SOUND_ON);
}

uint8_t starforce::dsw2() {
  return STARFORCE_DSW2;
}

unsigned char starforce::opZ80(unsigned short Addr) {
  if (current_cpu == 0)
    return rom_main_ptr[Addr];
  else
    return rom_sub_ptr[Addr];
}

unsigned char starforce::rdZ80(unsigned short Addr) {
  if (current_cpu == 0) {
    // --- Letture dalla CPU Principale ---
    // 0x0000 - 0x7FFF: ROM principale (32 KB)
    if (Addr < 0x8000)
      return rom_main_ptr[Addr];

    // 0x8000 - 0x8FFF: RAM Generica (4 KB)
    if (Addr >= 0x8000 && Addr <= 0x8FFF)
      return memory[STARFORCE_GENERAL_RAM + (Addr - 0x8000)];

    // 0x9000 - 0x93FF: Video RAM Foreground (1 KB)
    if (Addr >= 0x9000 && Addr <= 0x93FF)
      return memory[STARFORCE_FG_VIDEO_RAM + (Addr - 0x9000)];

    // 0x9400 - 0x97FF: Color RAM Foreground (1 KB)
    if (Addr >= 0x9400 && Addr <= 0x97FF)
      return memory[STARFORCE_FG_COLOR_RAM + (Addr - 0x9400)];

    // 0x9800 - 0x987F: Sprite RAM (128 bytes)
    if (Addr >= 0x9800 && Addr <= 0x987F)
      return memory[STARFORCE_SPRITE_RAM + (Addr - 0x9800)];

    // 0x9C00 - 0x9DFF: Palette RAM (512 bytes)
    if (Addr >= 0x9C00 && Addr <= 0x9DFF)
      return memory[STARFORCE_PALETTE_RAM + (Addr - 0x9C00)];

    // 0x9E00 - 0x9E3F: Registri di controllo (64 bytes)
    if (Addr >= 0x9E00 && Addr <= 0x9E3F)
      return memory[STARFORCE_HW_CONTROL_RAM + (Addr - 0x9E00)];

    // 0xA000 - 0xA7FF: Video RAM Background 3 (2 KB)
    if (Addr >= 0xA000 && Addr <= 0xA7FF)
      return memory[STARFORCE_BG3_VIDEO_RAM + (Addr - 0xA000)];

    // 0xA800 - 0xAFFF: Video RAM Background 2 (2 KB)
    if (Addr >= 0xA800 && Addr <= 0xAFFF)
       return memory[STARFORCE_BG2_VIDEO_RAM + (Addr - 0xA800)];

    // 0xB000 - 0xB7FF: Video RAM Background 1 (2 KB)
    if (Addr >= 0xB000 && Addr <= 0xB7FF)
       return memory[STARFORCE_BG1_VIDEO_RAM + (Addr - 0xB000)];

    // 0xB800 - 0xBBFF: Radar RAM (1 KB)
    if (Addr >= 0xB800 && Addr <= 0xBBFF)
      return memory[STARFORCE_RADAR_RAM + (Addr - 0xB800)];

    // 0xD000 - 0xD005: Input e DSW
    if (Addr >= 0xD000 && Addr <= 0xD005) {
      unsigned char retval = 0x00;
      unsigned char keymask = 0x00;
      switch (Addr)
      {
      case 0xD000:
        keymask = input->buttons_get();
	      if(keymask & BUTTON_RIGHT) retval |= 0x01;
	      if(keymask & BUTTON_LEFT)  retval |= 0x02;
	      if(keymask & BUTTON_UP)    retval |= 0x04;
	      if(keymask & BUTTON_DOWN)  retval |= 0x08;
        if(keymask & BUTTON_FIRE)  retval |= 0x10;
        return retval;
      case 0xD001:
        return 0x00; // Input Giocatore 2
      case 0xD002:
        keymask = input->buttons_get();
        if(keymask & BUTTON_START) retval |= 0x04;       
        if(keymask & BUTTON_COIN && !coinBackup) {
          coinFrameCounter = 2;
          coinBackup = 1;
        }
        
        //coin pulse for min 2 and max 9 frames...
        if (coinFrameCounter > 0) {
          retval |= 0x01; 
        }
        else {
          if ((keymask & BUTTON_COIN) == 0)
            coinBackup = 0;
        }
        return retval;
      case 0xD003:
        return 0x00; // NOPR in MAME (Not Read)
      case 0xD004:
        game_started = 1;
        return dsw1();
      case 0xD005:
        return dsw2();
      }
    }
  }
  else {
    // --- Letture dalla CPU Audio ---
    // 0x0000 - 0x1FFF: ROM Audio (8 KB)
    if (Addr < 0x2000)
      return rom_sub_ptr[Addr];

    // 0x4000 - 0x43FF: RAM Audio (1 KB)
    if (Addr >= 0x4000 && Addr <= 0x43FF)
      return memory[STARFORCE_SOUND_RAM + (Addr - 0x4000)];
  }
  return 0xFF;
}

void starforce::wrZ80(unsigned short Addr, unsigned char Value) {
  if (current_cpu == 0) {
    // 0x8000 - 0x8FFF: RAM Generica (4 KB)
    if (Addr >= 0x8000 && Addr <= 0x8FFF) {
      memory[STARFORCE_GENERAL_RAM + (Addr - 0x8000)] = Value;
      return;
    }

    // 0x9000 - 0x93FF: Video RAM Foreground (1 KB)
    if (Addr >= 0x9000 && Addr <= 0x93FF) {
      memory[STARFORCE_FG_VIDEO_RAM + (Addr - 0x9000)] = Value;
      return;
    }

    // 0x9400 - 0x97FF: Color RAM Foreground (1 KB)
    if (Addr >= 0x9400 && Addr <= 0x97FF) {
      memory[STARFORCE_FG_COLOR_RAM + (Addr - 0x9400)] = Value;
      return;
    }

    // 0x9800 - 0x987F: Sprite RAM (128 bytes)
    if (Addr >= 0x9800 && Addr <= 0x987F) {
      memory[STARFORCE_SPRITE_RAM + (Addr - 0x9800)] = Value;
      return;
    }

    // 0x9C00 - 0x9DFF: Palette RAM (512 bytes)
    if (Addr >= 0x9C00 && Addr <= 0x9DFF) {
      memory[STARFORCE_PALETTE_RAM + (Addr - 0x9C00)] = Value;
      starforce_palette[Addr - 0x9C00] = calculate_color_starforce(Value);
      return;
    }

    // 0x9E00 - 0x9E3F: Registri di controllo (64 bytes)
    if (Addr >= 0x9E00 && Addr <= 0x9E3F) {
      memory[STARFORCE_HW_CONTROL_RAM + (Addr - 0x9E00)] = Value;
      return;
    }

    // 0xA000 - 0xA7FF: Video RAM Background 3 (2 KB)
    if (Addr >= 0xA000 && Addr <= 0xA7FF) {
      memory[STARFORCE_BG3_VIDEO_RAM + (Addr - 0xA000)] = Value;
      return;
    }

    // 0xA800 - 0xAFFF: Video RAM Background 2 (2 KB)
    if (Addr >= 0xA800 && Addr <= 0xAFFF) {
      memory[STARFORCE_BG2_VIDEO_RAM + (Addr - 0xA800)] = Value;
      return;
    }

    // 0xB000 - 0xB7FF: Video RAM Background 1 (2 KB)
    if (Addr >= 0xB000 && Addr <= 0xB7FF) {
      memory[STARFORCE_BG1_VIDEO_RAM + (Addr - 0xB000)] = Value;
      return;
    }

    // 0xB800 - 0xBBFF: Radar RAM (1 KB)
    if (Addr >= 0xB800 && Addr <= 0xBBFF) {
      memory[STARFORCE_RADAR_RAM + (Addr - 0xB800)] = Value;
      return;
    }

    // 0xD000 - 0xD005: Controllo hardware
    if (Addr >= 0xD000 && Addr <= 0xD005) {
      switch (Addr)
      {
      case 0xD000: // flip_screen_w
        return;
      case 0xD002:
        //irq_ctrl_w: Used during power on selft test from main cpu (skip interrupts)
        return;
      case 0xD004: // Latch per il comando audio
        sound_latch = Value;
        sound_latch_pending = 1;
        return;
      }
    }
  }
  else {
    // --- Scritture dalla CPU Audio ---
    // 0x4000 - 0x43FF: RAM Audio (1 KB)
    if (Addr >= 0x4000 && Addr <= 0x43FF) {
      memory[STARFORCE_SOUND_RAM + (Addr - 0x4000)] = Value;
      return;
    }

    // 0x8000, 0x9000, 0xA000: Scrittura ai chip audio SN76489
    if (Addr == 0x8000) {
      SN76489_Write_3chip(0, Value); // Chiama direttamente la funzione di scrittura
      return;
    }
    if (Addr == 0x9000) {
      SN76489_Write_3chip(1, Value); // Chiama direttamente la funzione di scrittura
      return;
    }
    if (Addr == 0xA000) {
      SN76489_Write_3chip(2, Value); // Chiama direttamente la funzione di scrittura
      return;
    }

    // Aggiunta per il DAC (non essenziale per ora, ma completo)
    if (Addr == 0xD000) { /* DAC Volume */
      return;
    }

    if (Addr == 0xE000) { /* DAC Enable */
      return;
    }
  }
}

unsigned char starforce::inZ80(unsigned short Port) {
  // La CPU audio sta leggendo dalla porta dati
  if ((Port & 0xFF) == 0x00) {
    sound_latch_pending = 0; 
    return sound_latch;
  }
  return 0x00;
}

void starforce::outZ80(unsigned short Port, unsigned char Value) {
  // Initialize Z80pio and Z80ctc
  //09: z80ctc-ch2  tempo interrupt 88.778Hz
  //Port &= 0xFF;
  //printf("Out Port: %X, Value: %d\n", Port, Value);
}

void starforce::run_frame(void) {
  for (int i = 0; i < 1666; i++) {
    current_cpu = 0; StepZ80(&cpu[0]); StepZ80(&cpu[0]); StepZ80(&cpu[0]); StepZ80(&cpu[0]);
    current_cpu = 1; StepZ80(&cpu[1]); StepZ80(&cpu[1]);
    if (sound_irq_toggle && i == 832) {
      IntZ80(&cpu[1], sound_latch_pending ? 0x00 : 0x0A); // 30Hz; Vector 0x0A = daisy chain
    }
  }

  // 60Hz + 30Hz = ~88.778Hz
  sound_irq_toggle ^= 1;
  IntZ80(&cpu[1], sound_latch_pending ? 0x00 : 0x0A); // 60Hz; Vector 0x0A = daisy chain

  vblank.publish([this](VideoState &v) {
    const unsigned char *hw = &memory[STARFORCE_HW_CONTROL_RAM];
    memcpy(v.sprite_ram, &memory[STARFORCE_SPRITE_RAM], sizeof(v.sprite_ram));
    memcpy(v.bg_vram[0], &memory[STARFORCE_BG1_VIDEO_RAM], BG_MAP_SIZE);
    memcpy(v.bg_vram[1], &memory[STARFORCE_BG2_VIDEO_RAM], BG_MAP_SIZE);
    memcpy(v.bg_vram[2], &memory[STARFORCE_BG3_VIDEO_RAM], BG_MAP_SIZE);
    v.bg12_scroll_y = hw[0x30] + (hw[0x31] << 8);
    v.bg12_scroll_x = hw[0x35];
    v.bg3_scroll_y = hw[0x20] + (hw[0x21] << 8);
    v.bg3_scroll_x = hw[0x25];
  });

  current_cpu = 0;
  IntZ80(&cpu[0], INT_IRQ);

  if (coinFrameCounter > 0)
    coinFrameCounter--;
}

void starforce::SN76489_Write_3chip(int chip, unsigned char data) {
  if (data & 0x80) { // Latch
    int reg = (data >> 5) & 0x03; 
    int type = (data >> 4) & 0x01;
    sn_last_register[chip] = (reg * 2) + type;
        
    if (reg < 4) { 
      if (type == 0) { // Freq (4 bit bassi)
        sn_period[chip][reg] = (sn_period[chip][reg] & 0x3F0) | (data & 0x0F);
      } 
      else { // Volume
        sn_volume[chip][reg] = (data & 0x0F);
      }
    }
  } 
  else { // Dati
    int reg = sn_last_register[chip] / 2;
    int type = sn_last_register[chip] % 2;

    if (reg < 4 && type == 0) { // Se il latch era per una Frequenza
      if (reg == 3) { // Canale di rumore
        sn_period[chip][3] = data & 0x07;
      } 
      else { // Canali di tono
        sn_period[chip][reg] = (sn_period[chip][reg] & 0x00F) | ((data & 0x3F) << 4);
      }
    }
  }
}

void starforce::prepare_frame(void) {
  vblank.read(video);
  active_sprites = 0;

  // MAME itera al contrario, lo manteniamo per accuratezza.
  for (int i = 31; i >= 0 && active_sprites < 128; i--) {
    unsigned char *sprite_base_ptr = video.sprite_ram + (i * 4);

    unsigned char code_byte = sprite_base_ptr[0];
    unsigned char attr_byte = sprite_base_ptr[1];
    unsigned char y_byte = sprite_base_ptr[2]; // y originale del gioco verticale
    unsigned char x_byte = sprite_base_ptr[3]; // x originale del gioco verticale

    struct sprite_S *spr = &sprite[active_sprites];
    spr->is_32x32 = (code_byte & 0xC0) == 0xC0;
    // 32x32 sprites use the low 7 bits as index
    spr->code = spr->is_32x32 ? (code_byte & 0x7F) : code_byte;

    // MAME: native sx = x_byte, sy = 240 - y_byte (224 for 32x32). ROT90
    // onto the 288 row screen: row = sx + 16, left column =
    // 239 - sy - (size - 1) = y_byte - 16 for both sizes.
    spr->x = y_byte - 16;
    spr->y = x_byte + 16;

    short size = spr->is_32x32 ? 32 : 16;
    if (spr->x > 224 || (spr->x + size) < 0 || spr->y > 288 || (spr->y + size) < 0)
      continue;

    spr->color = attr_byte & 0x07;
    // native flipy (0x80) mirrors screen columns, native flipx (0x40) rows
    spr->flags = ((attr_byte & 0x80) ? 1 : 0) | ((attr_byte & 0x40) ? 2 : 0);
    spr->priority = (attr_byte & 0x30) >> 4;
    active_sprites++;
  }
}

inline unsigned short starforce::calculate_color_starforce(unsigned char raw_palette_byte) {
  // Passaggi 1-4: Calcolo dei componenti, identici a prima
  unsigned char i = (raw_palette_byte >> 6) & 0x03;
  unsigned char b_base = (raw_palette_byte >> 4) & 0x03;
  unsigned char g_base = (raw_palette_byte >> 2) & 0x03;
  unsigned char r_base = (raw_palette_byte >> 0) & 0x03;

  unsigned char r4 = r_base ? ((r_base << 2) | i) : 0;
  unsigned char g4 = g_base ? ((g_base << 2) | i) : 0;
  unsigned char b4 = b_base ? ((b_base << 2) | i) : 0;

  unsigned char r8 = r4 * 17;
  unsigned char g8 = g4 * 17;
  unsigned char b8 = b4 * 17;

  unsigned char r5 = r8 >> 3;
  unsigned char g6 = g8 >> 2;
  unsigned char b5 = b8 >> 3;

  // 5. Impacchettamento nel formato standard Little Endian
  unsigned short rgb565_le = (r5 << 11) | (g6 << 5) | b5;

  // 6. ESEGUI LO SWAP DEI BYTE
  // Questo trasforma 0xABCD in 0xCDAB
  return ((rgb565_le & 0xFF00) >> 8) | ((rgb565_le & 0x00FF) << 8);
}

void starforce::blit_background_line(short start_screen_row, int layer_num) {
  const uint32_t *base_tile_ptr;
  const unsigned char *vram_data;
  int scroll_x, scroll_y;
  int num_tiles_in_map = (layer_num == 3) ? 128 : 256;
  int palette_bank_offset;

  switch (layer_num) {
  case 1:
    base_tile_ptr = (const uint32_t *)bg_ptr[0];
    vram_data = video.bg_vram[0];
    scroll_x = video.bg12_scroll_x;
    scroll_y = video.bg12_scroll_y;
    palette_bank_offset = 64;
    break;
  case 2:
    base_tile_ptr = (const uint32_t *)bg_ptr[1];
    vram_data = video.bg_vram[1];
    scroll_x = video.bg12_scroll_x;
    scroll_y = video.bg12_scroll_y;
    palette_bank_offset = 128;
    break;
  case 3:
    base_tile_ptr = (const uint32_t *)bg_ptr[2];
    vram_data = video.bg_vram[2];
    scroll_x = video.bg3_scroll_x;
    scroll_y = video.bg3_scroll_y;
    palette_bank_offset = 192;
    break;
  }

  // The strip's 8 lines are 8 consecutive logical_x pixels, the same for
  // every column. They span at most two 8-pixel chunks (one packed uint32
  // each): pixels [shift..7] of chunk0, then [0..shift-1] of chunk1.
  //   logical_x: |c0 c0 c0 c0 c0 c0 c0 c0|c1 c1 c1 ...
  //   strip:              |0  1  2  3  4  5  6  7|   (shift = 5)
  // screen row 16 is native x 0; screen column c is native y 239 - c
  const int logical_x_start = start_screen_row - 16 + scroll_x;
  const int shift = logical_x_start & 7;
  const int split = 8 - shift; // first strip line taken from chunk1
  const int chunk0 = logical_x_start >> 3;
  const int chunk1 = chunk0 + 1;
  const int tile_col0 = (chunk0 >> 1) & 15;
  const int tile_col1 = (chunk1 >> 1) & 15;
  const int half0 = chunk0 & 1;
  const int half1 = chunk1 & 1;

  unsigned short *fb_col = &frame_buffer[223];

  for (int x_buffer = 0; x_buffer < 224; x_buffer++, fb_col--) {
    const int logical_y = x_buffer + 16 + scroll_y;
    const unsigned char *vram_row_ptr = vram_data + (((logical_y >> 4) & 31) << 4);
    const int y_tile_offset = (logical_y & 15) << 1;

    // tile code 0 and codes beyond the layer's tile count are transparent
    const unsigned char code0 = vram_row_ptr[tile_col0];
    const unsigned char code1 = vram_row_ptr[tile_col1];
    const bool valid0 = code0 > 0 && code0 < num_tiles_in_map;
    const bool valid1 = code1 > 0 && code1 < num_tiles_in_map;
    const uint32_t w0 = valid0 ? base_tile_ptr[(code0 << 5) + y_tile_offset + half0] : 0;
    const uint32_t w1 = valid1 ? base_tile_ptr[(code1 << 5) + y_tile_offset + half1] : 0;
    if ((w0 | w1) == 0) {
      continue;
    }

    // 8 strip pixels, 3 bits each, line 0 in the top bits
    const uint32_t bits = (w0 << (3 * shift)) | (w1 >> (24 - 3 * shift));
    const unsigned short *colors0 = &starforce_palette[palette_bank_offset + (bg_color_group(code0, layer_num) << 3)];
    const unsigned short *colors1 = &starforce_palette[palette_bank_offset + (bg_color_group(code1, layer_num) << 3)];

    unsigned short *fb_ptr = fb_col;
    for (int y_in_buffer = 0; y_in_buffer < 8; y_in_buffer++, fb_ptr += 224) {
      // pixel already drawn by a higher priority layer?
      if (*fb_ptr != 0) {
        continue;
      }

      const unsigned char px = (bits >> (21 - 3 * y_in_buffer)) & 0x07;
      if (px != 0) {
        *fb_ptr = (y_in_buffer < split ? colors0 : colors1)[px];
      }
    }
  }
}

// layer 1 stores the color group bits of the tile code shuffled
inline unsigned char starforce::bg_color_group(unsigned char tile_code, int layer_num) {
  if (layer_num != 1) {
    return tile_code >> 5;
  }

  const unsigned char bit0 = (tile_code >> 5) & 1;
  const unsigned char bit2 = (tile_code >> 6) & 1;
  const unsigned char bit1 = (tile_code >> 7) & 1;
  return bit1 | (bit0 << 1) | (bit2 << 2);
}

void starforce::blit_tile_fg(short row, char col) {
  unsigned short vram_addr = tileaddr[row][col];
  unsigned char base_tile_code = memory[STARFORCE_FG_VIDEO_RAM + vram_addr];
  unsigned char color_attr = memory[STARFORCE_FG_COLOR_RAM + vram_addr];
  unsigned int final_tile_code = base_tile_code + ((color_attr & 0x10) << 4); // 0x10 << 4 = 256
  const unsigned int *tile_data = fg_ptr[final_tile_code];
  const unsigned short *colors = &starforce_palette[(color_attr & 0x07) << 3]; // * 8 -> << 3

  // start address for column offset in framebuffer
  unsigned short *ptr = frame_buffer + (col << 3); // col * 8 -> col << 3

  // 8 lines of tile
  for (char c = 0; c < 8; c++) {
    unsigned int rowdata = tile_data[c];

    // Pixel 0 (r = 0): Shift 3 * (7 - 0) = 21
    unsigned char px = (rowdata >> 21) & 0x07;
    if (px != 0) ptr[0] = colors[px];

    // Pixel 1 (r = 1): Shift 3 * (7 - 1) = 18
    px = (rowdata >> 18) & 0x07;
    if (px != 0) ptr[1] = colors[px];

    // Pixel 2 (r = 2): Shift 3 * (7 - 2) = 15
    px = (rowdata >> 15) & 0x07;
    if (px != 0) ptr[2] = colors[px];

    // Pixel 3 (r = 3): Shift 3 * (7 - 3) = 12
    px = (rowdata >> 12) & 0x07;
    if (px != 0) ptr[3] = colors[px];

    // Pixel 4 (r = 4): Shift 3 * (7 - 4) = 9
    px = (rowdata >> 9) & 0x07;
    if (px != 0) ptr[4] = colors[px];

    // Pixel 5 (r = 5): Shift 3 * (7 - 5) = 6
    px = (rowdata >> 6) & 0x07;
    if (px != 0) ptr[5] = colors[px];

    // Pixel 6 (r = 6): Shift 3 * (7 - 6) = 3
    px = (rowdata >> 3) & 0x07;
    if (px != 0) ptr[6] = colors[px];

    // Pixel 7 (r = 7): Shift 3 * (7 - 7) = 0
    px = rowdata & 0x07;
    if (px != 0) ptr[7] = colors[px];

    // jump to next line of framebuffers
    ptr += 224;
  }
}

void starforce::blit_sprite(short row, unsigned char s_idx) {
  const struct sprite_S *spr = &sprite[s_idx];
  const short size = spr->is_32x32 ? 32 : 16;

  // vertical boundaries of actual 8-pixel strip
  const int y_strip_start = row << 3; 
  const int y_strip_end   = y_strip_start + 8;

  // Culling: is sprite in strip?
  const int spr_y_start = spr->y;
  const int spr_y_end   = spr_y_start + size;
  if (spr_y_start >= y_strip_end || spr_y_end <= y_strip_start)
    return;

  // calculate the exact area of ​​overlap (intersection for Y).
  int min_y_sprite = (spr_y_start < y_strip_start) ? (y_strip_start - spr_y_start) : 0;
  int max_y_sprite = (spr_y_end > y_strip_end) ? (y_strip_end - spr_y_start) : size;

  // precompute horizontal clipping boundaries for the X-loop.
  int spr_x_start = spr->x;
  int min_x_sprite = (spr_x_start < 0) ? -spr_x_start : 0;
  int max_x_sprite = (spr_x_start + size > 224) ? (224 - spr_x_start) : size;
  if (min_x_sprite >= max_x_sprite) 
    return; 

  const unsigned short *colors = &starforce_palette[320 + (spr->color << 3)];
  const bool flip_y = (spr->flags & 2) != 0;
  const bool flip_x = (spr->flags & 1) != 0;
  const int size_minus_1 = size - 1;

  // base-pointer 
  const uint32_t *sprite_data_ptr = spr->is_32x32 
    ? (const uint32_t *)spr32_ptr[spr->code]
    : (const uint32_t *)spr16_ptr[spr->code];

  for (int y_in_sprite = min_y_sprite; y_in_sprite < max_y_sprite; y_in_sprite++) {
    int y_in_buffer = (spr_y_start + y_in_sprite) - y_strip_start;
    int y_read = flip_y ? (size_minus_1 - y_in_sprite) : y_in_sprite;

    const uint32_t *sprite_row_gfx = sprite_data_ptr + (y_read * (spr->is_32x32 ? 4 : 2));

    // set pointer to begin of line in frame buffer
    unsigned short *fb_row_ptr = &frame_buffer[y_in_buffer * 224];

    for (int x_in_sprite = min_x_sprite; x_in_sprite < max_x_sprite; x_in_sprite++) {
      // 1. Calculate which pixel must be read from the sprite memory.
      int x_read = flip_x ? (size_minus_1 - x_in_sprite) : x_in_sprite;

      // Bit-Extraction
      int chunk_idx = x_read >> 3;   
      int x_in_chunk = x_read & 7;    

      uint32_t packed_data = sprite_row_gfx[chunk_idx];
      unsigned char px = (packed_data >> (3 * (7 - x_in_chunk))) & 0x07;

      if (px != 0) {
        // 2. Calculate the target coordinate on the screen
        int dest_x = spr_x_start + x_in_sprite;
        fb_row_ptr[dest_x] = colors[px];
      }
    }
  }
}

void starforce::render_row(short row) {
  if ((row < 2) || (row >= 34))
    return;

  for (unsigned char s = 0; s < active_sprites; s++) {
    blit_sprite(row, s);
  }

  for (char col = 0; col < 28; col++) {
    blit_tile_fg(row, col);
  }

  blit_background_line(row * 8, 1); // BG1
 
  blit_background_line(row * 8, 2); // BG2

  blit_background_line(row * 8, 3); // BG3
}

Asset<unsigned short, COMPRESSED> &starforce::logo(void) {
  return starforce_logo;
}

#ifdef LED_PIN
void starforce::gameLeds(CRGB *leds) {
  static char sub_cnt = 0;
  if(sub_cnt++ == 32) {
    sub_cnt = 0;
    static char led = 0;
    char il = (led < NUM_LEDS) ? led : ((2 * NUM_LEDS - 2) - led);
    for(char c = 0; c < NUM_LEDS; c++) {
      if(c == il) leds[c] = LED_WHITE;
      else        leds[c] = LED_BLUE;
    }
    led = (led + 1) % (2 * NUM_LEDS - 2);
  }
}

void starforce::menuLeds(CRGB *leds) {
  static const CRGB menu_leds[7] = {LED_BLUE, LED_CYAN, LED_BLUE, LED_WHITE, LED_BLUE, LED_CYAN, LED_BLUE};
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}
#endif