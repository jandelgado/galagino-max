#include "bombjack.h"
#include "bombjack_rom1.h"
#include "bombjack_rom2.h"
#include "bombjack_bg_maps.h"
#include "bombjack_bg_tiles.h"
#include "bombjack_fg_tiles.h"
#include "bombjack_sprites.h"

// Unpack eagerly, largest first: only this order fits Arena's two blocks.
// Also caches the pointers the hot paths read (see bombjack.h).
bombjack::bombjack()
  : bombjack_bg_maps(bombjack_bg_maps_blob),
    bombjack_bg_tiles(bombjack_bg_tiles_blob),
    bombjack_fg_tiles(bombjack_fg_tiles_blob),
    bombjack_rom_cpu1(bombjack_rom_cpu1_blob),
    bombjack_rom_cpu2(bombjack_rom_cpu2_blob),
    bombjack_sprites_16x16(bombjack_sprites_16x16_blob),
    bombjack_sprites_32x32(bombjack_sprites_32x32_blob) {
	rom_cpu1_ptr = bombjack_rom_cpu1.data();
	bombjack_sprites_32x32.data();
	bombjack_sprites_16x16.data();
	bombjack_fg_tiles.data();
	rom_cpu2_ptr = bombjack_rom_cpu2.data();
	bg_maps_ptr = bombjack_bg_maps.data();
}

void bombjack::reset() {
  machineBase::reset();
}

unsigned char bombjack::opZ80(unsigned short Addr) {
  if (current_cpu == 0)
    return rom_cpu1_ptr[Addr];
  else
    return rom_cpu2_ptr[Addr];
}

unsigned char bombjack::rdZ80(unsigned short Addr) {
  if (current_cpu == 0) {
    // --- Lettura dalle aree ROM mappate ---
    if (Addr >= 0x0000 && Addr <= 0x7FFF) { return rom_cpu1_ptr[Addr]; }
    if (Addr >= 0xC000 && Addr <= 0xDFFF) { return rom_cpu1_ptr[Addr]; }

    // --- Gestione delle aree RAM mappate ---
    if (Addr >= 0x8000 && Addr <= 0x8FFF) { return memory[Addr - 0x8000]; } // 4096 bytes
    if (Addr >= 0x9000 && Addr <= 0x93FF) { return memory[Addr - 0x9000 + 0x1000]; } // 1024 bytes
    if (Addr >= 0x9400 && Addr <= 0x97FF) { return memory[Addr - 0x9400 + 0x1000 + 0x400]; } // 1024 bytes
    if (Addr >= 0x9800 && Addr <= 0x987F) { return memory[Addr - 0x9800 + 0x1000 + 0x400 + 0x400]; } // 128 bytes
    if (Addr >= 0x9A00 && Addr <= 0x9A01) { return memory[Addr - 0x9A00 + 0x1000 + 0x400 + 0x400 + 0x80]; } // 2 bytes

    // --- Gestione delle porte di I/O (Input e DIP Switches) ---
    if ((Addr & 0xFFF8) == 0xB000) { // Controlla l'indirizzo base 0xB000 con mirror
      switch (Addr & 0xB007) { // Maschera per i mirror
      case PORT_P1_IN: {
        unsigned char keymask = input->buttons_get();
        unsigned char retval = 0x00; // Inizia con tutti i bit a 0 (rilasciato)

        // Il gioco si aspetta IP_ACTIVE_HIGH, quindi mettiamo il bit a 1 se il pulsante è premuto
        if (keymask & BUTTON_RIGHT)
          retval |= 0x01;
        if (keymask & BUTTON_LEFT)
          retval |= 0x02;
        if (keymask & BUTTON_UP)
          retval |= 0x04; // NOTA: Joystick UP e DOWN sono scambiati nel tuo codice originale
        if (keymask & BUTTON_DOWN)
          retval |= 0x08;
        if (keymask & BUTTON_FIRE && game_started) //skip service when fire is pressed
          retval |= 0x10;

        // Gli input di servizio (START/COIN) sono su un'altra porta, quindi qui non servono.
        return retval;
      }
      case PORT_P2_IN:
        return 0x00;
      case PORT_SYSTEM_IN: {
        unsigned char keymask1 = input->buttons_get();
        unsigned char retval1 = 0x00; // Inizia con tutti i bit a 0

        if (keymask1 & BUTTON_COIN)
          retval1 |= 0x01;
        if (keymask1 & BUTTON_START)
          retval1 |= 0x04;
        return retval1;
      }
      case PORT_WATCHDOG:
        return 0xFF; // Valore non importante
      case PORT_DSW1_IN:
        return BOMBJACK_DIP1 | (input->demoSoundsOff() ? BOMBJACK_DIP_DEMOSOUNDS_OFF : BOMBJACK_DIP_DEMOSOUNDS_ON);
      case PORT_DSW2_IN:
        return BOMBJACK_DIP2;
      }
    }
  }
  else if (current_cpu == 1) {
    Addr &= 0x7fff;   // a15 is unused

    // --- Audio CPU Read Logic ---
    // Handle ROM reads
    if (Addr <= 0x3FFF) { return rom_cpu2_ptr[Addr]; }

    // Handle RAM reads
    if (Addr >= 0x4000 && Addr <= 0x47FF) { return memory[(Addr - 0x4000 + 0x1000 + 0x400 + 0x400 + 0x80 + 2)]; }

    // Handle Sound Latch read
    if (Addr == 0x6000) {
      unsigned char value = sound_latch;
      if (sound_latch_backup != sound_latch)
        m_mmi_skip_audio_cpu = true;

      sound_latch_backup = sound_latch;
      sound_latch = 0;
      return value;
    }
  }

  // If no case matches, return a default value for an open bus
  return 0xFF;
}

void bombjack::wrZ80(unsigned short Addr, unsigned char Value) {
  if (current_cpu == 0) {
    // --- DEBUG MIRATO ---
    // Monitoriamo l'area di memoria dove dovrebbe apparire la scritta "SIDE-ONE".
    // L'hexdump mostra che è intorno all'offset 0x140.
    if (Addr == 0x9144 && Value == 83) {
      if (game_started == 0) {
        // Trovato il trigger! Imposta game_started a 1.
        // Questo sbloccherà la sincronizzazione in emulate_frame.
        game_started = 1;
        printf("game_started\n");
      }
    }
    // --- Fase 1: Scrittura nelle aree RAM mappate ---
    if (Addr >= 0x8000 && Addr <= 0x8FFF) {
      memory[Addr - 0x8000] = Value;
      return;
    }

    if (Addr >= 0x9000 && Addr <= 0x93FF) {
      memory[Addr - 0x9000 + 0x1000] = Value;
      return;
    }

    if (Addr >= 0x9400 && Addr <= 0x97FF) {
      memory[Addr - 0x9400 + 0x1000 + 0x400] = Value;
      return;
    }

    if (Addr >= 0x9800 && Addr <= 0x987F) {
      memory[Addr - 0x9800 + 0x1000 + 0x400 + 0x400] = Value;
      return;
    }

    if (Addr >= 0x9A00 && Addr <= 0x9A01) {
      memory[Addr - 0x9A00 + 0x1000 + 0x400 + 0x400 + 0x80] = Value;
      return;
    }

    if (Addr >= 0x9C00 && Addr <= 0x9CFF) {
      palette[Addr - 0x9C00] = Value;
      uint8_t b_p, b_d;
      if (Addr & 1) {
        b_p = palette[Addr - 0x9C01];
        b_d = Value;
      }
      else {
        b_p = Value;
        b_d = palette[Addr - 0x9C00 + 1];
      }

      int r = (b_p & 15) | (b_p << 4), g = (b_p & 240) | (b_p >> 4), b = (b_d & 15) | (b_d << 4);
      uint16_t c = ((r & 248) << 8) | ((g & 252) << 3) | (b >> 3);

      int i = (Addr - 0x9C00) / 2;
      bombjack_palette[i] = (c >> 8) | (c << 8);
      return;
    }

    if (Addr == 0x9E00) {
      m_bg_image = Value;
      return;
    }

    if (Addr == 0xB000) {
      m_nmi_on = (Value & 0x01);
      return;
    }

    if (Addr == 0xB003) {
      return;
    }

    if (Addr == 0xB004) {
      m_flip = (Value & 0x01);
      return;
    }

    if (Addr == 0xB800) {
      sound_latch = Value;
      return;
    }

    return; // Se l'indirizzo non corrisponde a nulla, non facciamo niente.
  }
  else if (current_cpu == 1) {
    Addr &= 0x7fff;   // a15 is unused

    // Scrittura nella RAM audio
    if (Addr >= 0x4000 && Addr <= 0x47ff) {
      memory[(Addr - 0x4000 + 0x1000 + 0x400 + 0x400 + 0x80 + 2)] = Value; //& 0x07FF
      return;
    }
  }
}

void bombjack::outZ80(unsigned short Port, unsigned char Value) {
  int chip_id = -1;
  if ((Port & 0xFF) <= 0x01)
    chip_id = 0;
  else if ((Port & 0xFF) >= 0x10 && (Port & 0xFF) <= 0x11)
    chip_id = 1;
  else if ((Port & 0xFF) >= 0x80 && (Port & 0xFF) <= 0x81)
    chip_id = 2;
  else
    return;

  if ((Port & 1) == 0) { // Scrittura all'indirizzo del registro
    ay_address[chip_id] = Value & 0x0F;
  }
  else { // Scrittura ai dati del registro
    soundregs[(chip_id * 16) + ay_address[chip_id]] = Value;
  }
}

unsigned char bombjack::inZ80(unsigned short Port) {
  if ((Port & 0xFF) == 0x01)
    return soundregs[ay_address[0]];
  else if ((Port & 0xFF) == 0x11)
    return soundregs[(1 * 16) + ay_address[1]];
  else if ((Port & 0xFF) == 0x81)
    return soundregs[(2 * 16) + ay_address[2]];
  else
    return 0xff;
}

void bombjack::run_frame(void) {
  for (int i = 0; i < INST_PER_FRAME; i++) {
    current_cpu = 0; StepZ80(&cpu[0]); StepZ80(&cpu[0]); StepZ80(&cpu[0]); StepZ80(&cpu[0]);
    current_cpu = 1; StepZ80(&cpu[1]); StepZ80(&cpu[1]); StepZ80(&cpu[1]);
  }

  // NMI per la CPU Principale (già corretto, è GATED)
  if (m_nmi_on) {
    current_cpu = 0;
    IntZ80(&cpu[0], INT_NMI);
  }

  if (m_mmi_skip_audio_cpu) {
    m_mmi_skip_audio_cpu = false;
    return;
  }

  // NMI per la CPU Audio (DEVE ESSERE INCONDIZIONATO)
  current_cpu = 1;
  IntZ80(&cpu[1], INT_NMI);
}

void bombjack::prepare_frame(void) {
  uint8_t large_sprite_rev = (memory[0 + 0x1000 + 0x400 + 0x400 + 0x80] > memory[1 + 0x1000 + 0x400 + 0x400 + 0x80]) ? 1 : 0;
  uint8_t large_sprite_start_idx = memory[large_sprite_rev + 0x1000 + 0x400 + 0x400 + 0x80];
  uint8_t large_sprite_end_idx = memory[(large_sprite_rev + 0x1000 + 0x400 + 0x400 + 0x80) ^ 1];
  active_sprites = 0;

  // Itera sui 32 slot degli sprite hardware, in ordine inverso (da 31 a 0)
  for (int idx = 31; idx >= 0; idx--) {
    if (active_sprites >= 32)
      break;

    unsigned char *sprite_base_ptr = (memory + 0x1000 + 0x400 + 0x400) + 4 * (31 - idx);
    uint8_t code_low = sprite_base_ptr[0];

    // Salta gli sprite inattivi
    if (code_low == 0 && sprite_base_ptr[2] == 0 && sprite_base_ptr[3] == 0)
      continue;

    struct sprite_S spr;

    spr.is_32x32 = (((31 - idx) / 2) > large_sprite_start_idx) && (((31 - idx) / 2) <= large_sprite_end_idx);

    if (spr.is_32x32) {
      if (((31 - idx) % 2) != 0)
        continue;
      spr.code = code_low & 0x3F;
    }
    else {
      spr.code = code_low;
    }

    spr.color_block = (sprite_base_ptr[1] & 0x0F) << 3;
    spr.flip_x = (sprite_base_ptr[1] & 0x40) != 0;
    spr.flip_y = (sprite_base_ptr[1] & 0x80) != 0;

    spr.x = sprite_base_ptr[2] - 16;
    spr.y = sprite_base_ptr[3];

    if (spr.is_32x32)
      spr.x -= 1;
   
    // printf("Sprite code=0x%02X, size=%s, x=%4d, y=%4d, color=%d, flip(X:%d,Y:%d)\n",spr.code,(spr.is_32x32 ? "32x32" : "16x16"),spr.x,spr.y,spr.color_block >> 3, spr.flip_x,spr.flip_y);
    if ((spr.y > -16) && (spr.y < 288) && (spr.x > -16) && (spr.x < 224))
      sprite[active_sprites++] = spr;
  }
}

// frame_buffer arrives cleared (renderRow()), so pen 0 and an invisible
// background need no writes.
void bombjack::blit_tile_bg(short logical_row) {
  if ((m_bg_image & 0x10) == 0) {
    return;
  }

  // ROT90 with a (16, 24) visual offset: screen (x, y) shows source pixel
  // (y - 24, 239 - x). The strip's 8 lines are 8 consecutive source columns
  // inside one 8 pixel half of one 16x16 tile column, i.e. one packed
  // uint32 per screen column, line 0 in the top bits.
  const int source_x = logical_row * 8 - 24;
  if (source_x < 0 || source_x >= 224) {
    return;
  }

  const uint8_t *map = bg_maps_ptr + (m_bg_image & 7) * 0x0200 + (source_x >> 4);
  const int half = (source_x >> 3) & 1;

  // screen x = 0..223 is source y = 239..16, tile rows 14 down to 1
  unsigned short *fb = frame_buffer;
  for (int map_row = 14; map_row >= 1; map_row--) {
    const uint8_t tile_code = map[map_row * 16];
    const uint8_t attr = map[map_row * 16 + 0x100];
    const uint16_t *colors = &bombjack_palette[(attr & 0x0F) << 3];
    const uint32_t *gfx = bombjack_bg_tiles[tile_code] + half;
    const int flip_y = (attr & 0x80) ? 15 : 0;

    for (int pixel_y = 15; pixel_y >= 0; pixel_y--, fb++) {
      const uint32_t packed = gfx[(pixel_y ^ flip_y) * 2];
      if (packed == 0) {
        continue;
      }

      unsigned short *p = fb;
      for (int shift = 21; shift >= 0; shift -= 3, p += 224) {
        const uint8_t pen = (packed >> shift) & 0x07;
        if (pen != 0) {
          *p = colors[pen];
        }
      }
    }
  }
}

void bombjack::blit_tile_fg(short row, char col) {
  if ((row < 2) || (row >= 34))
    return;

  unsigned short tilemap_index = tileaddr[row][col];

  uint8_t chr = memory[tilemap_index + 0x1000];
  uint8_t clr = memory[tilemap_index + 0x1000 + 0x400];

  uint16_t tile_id = chr | ((clr & 0x10) << 4);
  uint8_t color_block = (clr & 0x0F) << 3;

  const uint32_t *tile_gfx = bombjack_fg_tiles[tile_id];
  unsigned short *ptr = frame_buffer + (col * 8);

  for (char r = 0; r < 8; r++, ptr += 224) {
    uint32_t packed_pixels = *tile_gfx++;
    if (packed_pixels == 0) {
      continue;
    }

    for (char c = 0; c < 8; c++) {
      uint8_t pen = (packed_pixels >> (3 * (7 - c))) & 0x07;

      if (pen != 0)
        ptr[c] = bombjack_palette[color_block | pen];
    }
  }
}

void bombjack::blit_sprite(short row, unsigned char s_idx) {
  if ((row < 2) || (row >= 34))
    return;

  if (s_idx >= active_sprites)
    return;

  struct sprite_S *s = &sprite[s_idx];
  int size = s->is_32x32 ? 32 : 16;

  const uint32_t *sprite_gfx_data;
  if (s->is_32x32) {
    if (s->code >= 64)
      return;

    sprite_gfx_data = bombjack_sprites_32x32[s->code];
  }
  else {
    if (s->code >= 256)
      return;

    sprite_gfx_data = bombjack_sprites_16x16[s->code];
  }

  // clip the sprite's box to the strip and the screen width
  const int strip_start_y = (row - 2) * 8;
  const int y0 = s->y > strip_start_y ? s->y : strip_start_y;
  const int y1 = (s->y + size < strip_start_y + 8) ? s->y + size : strip_start_y + 8;
  if (y0 >= y1) {
    return;
  }

  const int x0 = s->x > 0 ? s->x : 0;
  const int x1 = (s->x + size < 224) ? s->x + size : 224;

  // ROT90: screen column x is sprite line sy, screen line y is sprite
  // column sx, 8 pixels (3 bits each) per packed uint32. XOR with
  // size - 1 mirrors (16/32 are powers of 2); ROT90 itself mirrors sy.
  const int last = size - 1;
  const int sx_mirror = s->flip_x ? last : 0;
  const int sy_mirror = s->flip_y ? 0 : last;
  const uint16_t *colors = &bombjack_palette[s->color_block];

  for (int x = x0; x < x1; x++) {
    const int sy = (x - s->x) ^ sy_mirror;
    const uint32_t *line = sprite_gfx_data + ((sy * size) >> 3);
    unsigned short *fb = frame_buffer + (y0 - strip_start_y) * 224 + x;

    for (int y = y0; y < y1; y++, fb += 224) {
      const int sx = (y - s->y) ^ sx_mirror;
      const uint8_t pen = (line[sx >> 3] >> (3 * (7 - (sx & 7)))) & 0x07;
      if (pen != 0) {
        *fb = colors[pen];
      }
    }
  }
}

void bombjack::render_row(short row) {
  blit_tile_bg(row);
  
  for (char col = 0; col < 28; col++) {
    blit_tile_fg(row, col);
  }

  // render sprites
  for (unsigned char s = 0; s < active_sprites; s++) {
    blit_sprite(row, s);
  }
}

Asset<unsigned short, COMPRESSED> &bombjack::logo(void) {
  return bombjack_logo;
}

#ifdef LED_PIN
void bombjack::gameLeds(CRGB *leds) {
  static char sub_cnt = 0;
  if(sub_cnt++ == 16) {
    sub_cnt = 0;
    static char step = 0;
    for(char c = 0; c < NUM_LEDS; c++) {
      leds[c] = LED_BLACK;
    }
    if(step < NUM_LEDS - 1) {
      leds[step] = LED_YELLOW;
      leds[NUM_LEDS - 1] = LED_RED;
    } else {
      for(char c = 0; c < NUM_LEDS; c++) {
        leds[c] = (step % 2 == 0) ? LED_RED : LED_MAGENTA;
      }
    }
    step = (step + 1) % (NUM_LEDS + 4);
  }
}

void bombjack::menuLeds(CRGB *leds)
{
  static const CRGB menu_leds[7] = {LED_BLUE, LED_RED, LED_YELLOW, LED_WHITE, LED_YELLOW, LED_RED, LED_BLUE};
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}
#endif
