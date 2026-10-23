"""
Pooyan ROM converter per GALAGINO
Konami 1982 - set MAME "pooyan" - hardware gemello di Time Pilot ma gfx 4bpp

ROM set:
  Program: 1.4a + 2.5a + 3.6a + 4.7a (4x8K) = 32KB main CPU
  Sound:   xx.7a + xx.8a (2x4K) = 8KB sound CPU (timeplt_audio)
  Tiles:   8.10g (meta' 1) + 7.9g (meta' 2) = 256 tile 8x8 4bpp
  Sprites: 6.9a (meta' 1) + 5.8a (meta' 2) = 64 sprite 16x16 4bpp
  PROMs:   pooyan.pr1 (32B) = palette
           pooyan.pr3 (256B) = char lookup  (16 gruppi x 16 pen, |0x10)
           pooyan.pr2 (256B) = sprite lookup (16 gruppi x 16 pen)

Layout gfx MAME (charlayout/spritelayout): 4 piani
  { RGN_FRAC(1,2)+4, RGN_FRAC(1,2)+0, 4, 0 }  (primo = MSB), bit MSB-first:
  per xo=col%4: bit3=(b2>>(3-xo))&1, bit2=(b2>>(7-xo))&1,
                bit1=(b1>>(3-xo))&1, bit0=(b1>>(7-xo))&1
  con b1 = byte nella prima meta' ROM, b2 = stesso offset nella seconda.
"""
from romconv.core.context import Context
from romconv.core.job import Job
from romconv.emit.asset import emit_compressed
from romconv.gfx.color import prom_rgb_332_resnet, rgb565_swapped
from romconv.gfx.gfxutil import hex16


# ---- Palette PROM (pr1, 32 colori) -> RGB565 byte-swapped ----
# MAME pooyan_state::palette: R = bit0-2 (1k/470/220), G = bit3-5 (1k/470/220),
# B = bit6-7 (470/220). Pesi normalizzati compute_resistor_weights:
# 1k/470/220 -> 33,71,151 ; 470/220 -> 81,174 (come rocnrope)
def _convert_palette(prom):
    return [rgb565_swapped(*prom_rgb_332_resnet(prom[i])) for i in range(32)]  # clamped, byte-swapped for SPI

# ---- Colormap: 16 gruppi x 16 pen per char e sprite ----
def _build_colormap(base_palette, prom_char, prom_sprite):
    # chars: entry = (lut & 0x0F) | 0x10  -> palette 16-31
    char_colors = [base_palette[(prom_char[i] & 0x0F) | 0x10] for i in range(256)]
    # sprites: entry = lut & 0x0F -> palette 0-15
    sprite_colors = [base_palette[prom_sprite[i] & 0x0F] for i in range(256)]
    # trasparenza sprite (MAME transpen_mask(gfx,color,0)):
    # pen trasparente se il valore LUT (colore indiretto) e' 0
    transmask = []
    for grp in range(16):
        mask = 0
        for pen in range(16):
            if (prom_sprite[grp * 16 + pen] & 0x0F) == 0:
                mask |= (1 << pen)
        transmask.append(mask)
    return char_colors, sprite_colors, transmask

# ---- Decodifica pixel 4bpp da due meta' ROM ----
def _pixel4(half1, half2, byte_off, xo):
    b1 = half1[byte_off]
    b2 = half2[byte_off]
    bit3 = (b2 >> (3 - xo)) & 1
    bit2 = (b2 >> (7 - xo)) & 1
    bit1 = (b1 >> (3 - xo)) & 1
    bit0 = (b1 >> (7 - xo)) & 1
    return (bit3 << 3) | (bit2 << 2) | (bit1 << 1) | bit0

# ---- Tiles 8x8 4bpp: 256 tile, 16 byte/tile per meta' ----
# xoffset {0-3, 64-67} -> col>=4 = +8 byte; yoffset row*8 bit -> +row byte
# Rotazione 90 CW per portrait (come timeplt): portrait[py][px] = landscape[7-px][py]
def _convert_tiles(half1, half2):
    tiles = []
    for t in range(256):
        base = t * 16
        landscape = [[_pixel4(half1, half2, base + row + (8 if col >= 4 else 0), col % 4)
                      for col in range(8)] for row in range(8)]
        packed = []
        for py in range(8):
            row_val = 0
            for px in range(8):
                row_val |= landscape[7 - px][py] << (px * 4)
            packed.append(row_val)
        tiles.append(packed)
    return tiles

# ---- Sprites 16x16 4bpp: 64 sprite, 64 byte/sprite per meta' ----
# xoffset {0-3, 64-67, 128-131, 192-195} -> gruppo col*8 byte
# yoffset {0..56, 256..312} -> righe 8-15 a +32 byte (24+row)
# Landscape senza rotazione, non flippati (pooyan::blit_sprite applica il
# flip a runtime), riga impacchettata come 8 byte nibble (col pari = nibble basso).
def _convert_sprites(half1, half2):
    sprites = []
    for s in range(64):
        base = s * 64
        rows = []
        for row in range(16):
            row_byte = row if row < 8 else 24 + row
            row_bytes = [0] * 8
            for col in range(16):
                row_bytes[col >> 1] |= _pixel4(half1, half2, base + row_byte + (col // 4) * 8, col % 4) << ((col & 1) * 4)
            rows.append(row_bytes)
        sprites.append(rows)
    return sprites

# ---- Scrittura header ----
def _write_bytes(ctx, filename, comment, name, data):
    with ctx.header(filename) as f:
        f.write("// {} ({} bytes)\n".format(comment, len(data)))
        emit_compressed(f, name, "unsigned char", "", len(data), list(data))
    ctx.log.info("Written: {} ({} bytes)".format(filename, len(data)))

def _write_tilemap(ctx, filename, tiles, char_colors):
    with ctx.header(filename) as f:
        f.write("// Pooyan tilemap: {} tile, 8x8, 4bpp, ruotati 90CW (portrait)\n".format(len(tiles)))
        f.write("// pixel = (riga >> (px*4)) & 0xF\n")
        flat = [r for rows in tiles for r in rows]
        emit_compressed(f, "pooyan_tilemap", "uint32_t", "[8]", len(tiles), flat)
        f.write("\n// Pooyan char colormap: 16 gruppi x 16 pen, RGB565 byte-swapped\n")
        f.write("const unsigned short pooyan_char_colormap[][16] = {\n")
        for grp in range(16):
            colors = char_colors[grp*16 : grp*16+16]
            f.write("  { " + ", ".join(hex16(c) for c in colors) + " }")
            f.write("," if grp < 15 else "")
            f.write("  // gruppo {}\n".format(grp))
        f.write("};\n")
    ctx.log.info("Written: {} ({} tiles, 16 char groups)".format(filename, len(tiles)))

def _write_spritemap(ctx, filename, sprites, sprite_colors, transmask):
    num = len(sprites)
    with ctx.header(filename) as f:
        f.write("// Pooyan spritemap: {} sprite, 16x16, 4bpp, non flippati, landscape\n".format(num))
        f.write("// pixel col c = (riga[c>>1] >> ((c&1)*4)) & 0xF\n")
        flat = [b for rows in sprites for rb in rows for b in rb]
        emit_compressed(f, "pooyan_spritemap", "unsigned char", "[16][8]", num, flat)
        f.write("\n// Pooyan sprite colormap: 16 gruppi x 16 pen, RGB565 byte-swapped\n")
        f.write("const unsigned short pooyan_sprite_colormap[][16] = {\n")
        for grp in range(16):
            colors = sprite_colors[grp*16 : grp*16+16]
            f.write("  { " + ", ".join(hex16(c) for c in colors) + " }")
            f.write("," if grp < 15 else "")
            f.write("  // gruppo {}\n".format(grp))
        f.write("};\n\n")
        f.write("// Maschera trasparenza per gruppo (bit N = pen N trasparente, MAME transpen_mask)\n")
        f.write("const unsigned short pooyan_sprite_transmask[16] = {\n  ")
        f.write(", ".join(hex16(m) for m in transmask))
        f.write("\n};\n")
    ctx.log.info("Written: {} ({} sprites x 4, 16 groups + transmask)".format(filename, num))

def _convert(ctx: Context) -> None:
    program = ctx.rom("1.4a") + ctx.rom("2.5a") + ctx.rom("3.6a") + ctx.rom("4.7a")
    snd     = ctx.rom("xx.7a") + ctx.rom("xx.8a")
    tiles1  = ctx.rom("8.10g");  tiles2  = ctx.rom("7.9g")
    spr1    = ctx.rom("6.9a");   spr2    = ctx.rom("5.8a")
    pr1     = ctx.rom("pooyan.pr1")
    pr2     = ctx.rom("pooyan.pr2")   # sprite lookup
    pr3     = ctx.rom("pooyan.pr3")   # char lookup

    assert len(program) == 0x8000 and len(snd) == 0x2000
    assert len(tiles1) == 0x1000 and len(tiles2) == 0x1000
    assert len(spr1) == 0x1000 and len(spr2) == 0x1000
    assert len(pr1) == 32 and len(pr2) == 256 and len(pr3) == 256

    _write_bytes(ctx, "pooyan_rom.h", "Pooyan program ROM", "pooyan_rom", program)
    _write_bytes(ctx, "pooyan_snd_rom.h", "Pooyan sound ROM", "pooyan_snd_rom", snd)

    base_palette = _convert_palette(pr1)
    char_colors, sprite_colors, transmask = _build_colormap(base_palette, pr3, pr2)

    _write_tilemap(ctx, "pooyan_tilemap.h", _convert_tiles(tiles1, tiles2), char_colors)
    _write_spritemap(ctx, "pooyan_spritemap.h", _convert_sprites(spr1, spr2),
                    sprite_colors, transmask)

    # NB: pooyan_dipswitches.h e' mantenuto a mano, non viene sovrascritto
    ctx.log.info("--- OPERAZIONE COMPLETATA ---")


JOB = Job(name="pooyan", convert=_convert, machine="pooyan", zips=("pooyan.zip",))
