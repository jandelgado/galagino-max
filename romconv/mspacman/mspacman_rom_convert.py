#!/usr/bin/env python3
"""
Convertitore ROM Ms. Pac-Man con applicazione patch lineare
- Genera il file spritemap temporaneo (come il convertitore originale)
- Applica le patch linearmente valore per valore per posizione
- Produce il file finale corretto
- Elimina i file temporanei
"""

import os
import sys

sys.path.insert(0, os.path.join("..", "pyconv"))
from romdata_emit import emit_compressed

ROM_SRC = os.path.normpath(os.path.join("..", "roms"))
OUT_DIR = os.path.normpath(os.path.join("..", "..", "source", "src", "machines", "mspacman"))

def load_file(name):
    path = os.path.join(ROM_SRC, name)
    if not os.path.exists(path):
        for alt in [name.lower(), name.upper()]:
            alt_path = os.path.join(ROM_SRC, alt)
            if os.path.exists(alt_path):
                path = alt_path
                break
    if not os.path.exists(path):
        print(f"ERRORE: File '{name}' non trovato in {os.path.abspath(ROM_SRC)}")
        return None
    with open(path, "rb") as f:
        return bytearray(f.read())

def decode_byte(b):
    return (((b>>0)&1)<<7 | ((b>>4)&1)<<6 | ((b>>5)&1)<<5 |
            ((b>>7)&1)<<4 | ((b>>6)&1)<<3 | ((b>>3)&1)<<2 |
            ((b>>2)&1)<<1 | ((b>>1)&1)<<0)

def addr_swap11(i):
    return (((i>>8)&1)<<10 | ((i>>7)&1)<<9  | ((i>>5)&1)<<8  |
            ((i>>9)&1)<<7  | ((i>>10)&1)<<6 | ((i>>6)&1)<<5  |
            ((i>>3)&1)<<4  | ((i>>4)&1)<<3  | ((i>>2)&1)<<2  |
            ((i>>1)&1)<<1  | ((i>>0)&1)<<0)

def addr_swap12(i):
    return (((i>>11)&1)<<11 | ((i>>3)&1)<<10 | ((i>>7)&1)<<9  |
            ((i>>9)&1)<<8   | ((i>>10)&1)<<7 | ((i>>8)&1)<<6  |
            ((i>>6)&1)<<5   | ((i>>5)&1)<<4  | ((i>>4)&1)<<3  |
            ((i>>2)&1)<<2   | ((i>>1)&1)<<1  | ((i>>0)&1)<<0)

def load_patch_values(patch_filename):
    """Carica i delta dal file patch (un valore per riga)"""
    if not os.path.exists(patch_filename):
        print(f"  ATTENZIONE: {patch_filename} non trovato, nessuna patch applicata")
        return None
    
    try:
        with open(patch_filename, 'r') as f:
            lines = [line.strip() for line in f if line.strip()]
        
        patch_values = []
        for line in lines:
            val = int(line, 16)
            patch_values.append(val)
        
        non_zero = sum(1 for v in patch_values if v != 0)
        return patch_values
        
    except Exception as e:
        print(f"  ERRORE caricamento {patch_filename}: {e}")
        return None

def apply_patch_values(values, patch_values, mask):
    """Applica le patch per posizione direttamente sui valori interi (usato
    quando l'array e' compresso: non esiste piu' un testo C su cui applicare
    la patch a regex)."""
    if patch_values is None:
        return values
    n = min(len(values), len(patch_values))
    out = list(values)
    for i in range(n):
        if patch_values[i] != 0:
            out[i] = (out[i] + patch_values[i]) & mask
    return out

def generate_sprite_values(gfx5e, gfx5f):
    sprite_offset = 2048
    max_gfx_size = len(gfx5f)
    SPRITE_COUNT = 64
    
    all_values = []
    
    for flip in range(4):
        for s in range(SPRITE_COUNT):
            for r in range(16):
                base = sprite_offset + (s * 64) + r
                
                b0l = gfx5f[base] if base < max_gfx_size else 0
                b0h = gfx5f[base + 16] if (base + 16) < max_gfx_size else 0
                b1l = gfx5f[base + 32] if (base + 32) < max_gfx_size else 0
                b1h = gfx5f[base + 48] if (base + 48) < max_gfx_size else 0
                
                wl, wh = 0, 0
                for bit in range(8):
                    p0l, p1l = (b0l >> (7-bit)) & 1, (b1l >> (7-bit)) & 1
                    p0h, p1h = (b0h >> (7-bit)) & 1, (b1h >> (7-bit)) & 1
                    wl |= (((p1l << 1) | p0l) << (bit * 2))
                    wh |= (((p1h << 1) | p0h) << (bit * 2))
                val = (wh << 16) | wl
                
                if flip & 1:
                    new_val = 0
                    for c in range(16):
                        pixel = (val >> (c * 2)) & 3
                        new_val |= (pixel << ((15 - c) * 2))
                    val = new_val
                
                all_values.append(val)

    return all_values

def main():
    if not os.path.exists(OUT_DIR):
        os.makedirs(OUT_DIR, exist_ok=True)

    # Carica i ROM
    pac6e = load_file("pacman.6e")
    pac6f = load_file("pacman.6f")
    pac6h = load_file("pacman.6h")
    u5 = load_file("u5")
    u6 = load_file("u6")
    u7 = load_file("u7")
    gfx5e = load_file("5e")
    gfx5f = load_file("5f")

    if not all(v is not None for v in [pac6e, pac6f, pac6h, u5, u6, u7, gfx5e, gfx5f]):
        return

    # 1. CPU ROM
    print("Generazione CPU ROM...")
    drom = bytearray([0xFF] * 0xC000)
    for i in range(0x1000):
        drom[0x0000 + i] = pac6e[i]
        drom[0x1000 + i] = pac6f[i]
        drom[0x2000 + i] = pac6h[i]
    for i in range(0x1000):
        drom[0x3000 + i] = decode_byte(u7[addr_swap12(i) & 0xFFF])
    for i in range(0x800):
        drom[0x8000 + i] = decode_byte(u5[addr_swap11(i) & 0x7FF])
        drom[0x8800 + i] = decode_byte(u6[(addr_swap12(i) & 0x7FF) + 0x800])
        drom[0x9000 + i] = decode_byte(u6[addr_swap12(i) & 0x7FF])
        drom[0x9800 + i] = pac6f[0x800 + i]

    # Patch CPU (originali)
    patches = [(0x0410, 0x8008), (0x08E0, 0x81D8), (0x0A30, 0x8118), (0x0BD0, 0x80D8),
               (0x0C20, 0x8120), (0x0E58, 0x8168), (0x0EA8, 0x8198), (0x1000, 0x8020),
               (0x1008, 0x8010), (0x1288, 0x8098), (0x1348, 0x8048), (0x1688, 0x8088),
               (0x16B0, 0x8188), (0x16D8, 0x80C8), (0x16F8, 0x81C8), (0x19A8, 0x80A8),
               (0x19B8, 0x81A8), (0x2060, 0x8148), (0x2108, 0x8018), (0x21A0, 0x81A0),
               (0x2298, 0x80A0), (0x23E0, 0x80E8), (0x2418, 0x8000), (0x2448, 0x8058),
               (0x2470, 0x8140), (0x2488, 0x8080), (0x24B0, 0x8180), (0x24D8, 0x80C0),
               (0x24F8, 0x81C0), (0x2748, 0x8050), (0x2780, 0x8090), (0x27B8, 0x8190),
               (0x2800, 0x8028), (0x2B20, 0x8100), (0x2B30, 0x8110), (0x2BF0, 0x81D0),
               (0x2CC0, 0x80D0), (0x2CD8, 0x80E0), (0x2CF0, 0x81E0), (0x2D60, 0x8160)]
    for dst, src in patches:
        for i in range(8):
            drom[dst + i] = drom[src + i]

    # 2. TILEMAP
    print("Generazione mspacman_tilemap.h...")
    tile_count = 256
    tile_base = []
    
    for t in range(tile_count):
        for row in range(8):
            idx = t * 8 + row
            b0, b1 = gfx5e[idx], gfx5f[idx]
            val = 0
            for bit in range(8):
                p0, p1 = (b0 >> (7-bit)) & 1, (b1 >> (7-bit)) & 1
                val |= (((p1 << 1) | p0) << (bit * 2))
            tile_base.append(val)
    
    tile_patch = load_patch_values("til_patch.table")
    if tile_patch:
        for i in range(len(tile_base)):
            if i < len(tile_patch) and tile_patch[i] != 0:
                tile_base[i] = (tile_base[i] + tile_patch[i]) & 0xFFFF
    
    with open(os.path.join(OUT_DIR, "mspacman_tilemap.h"), "w") as f:
        emit_compressed(f, "mspacman_tilemap", "unsigned short", "[8]", tile_count, tile_base)
    print(f"  Scritto mspacman_tilemap.h")

    # 3. SPRITEMAP - genera i valori base, applica la patch per posizione
    # direttamente sulla lista piatta (non piu' a regex su testo C, ora che
    # l'array e' compresso), poi scrive il file finale.
    print("\n=== GENERAZIONE SPRITEMAP ===")

    final_file = os.path.join(OUT_DIR, "mspacman_spritemap.h")
    sprite_values = generate_sprite_values(gfx5e, gfx5f)
    print(f"  Generati {len(sprite_values)} valori")

    sprite_patch = load_patch_values("spr_patch.table")
    sprite_values = apply_patch_values(sprite_values, sprite_patch, 0xFFFFFFFF)

    with open(final_file, 'w', encoding='utf-8') as f:
        emit_compressed(f, "mspacman_sprites", "unsigned long", "[64][16]", 4, sprite_values)

    print(f"  File finale: {final_file}")

    # 4. ROM FILES
    print("\nGenerazione ROM files...")
    with open(os.path.join(OUT_DIR, "mspacman_pacrom.h"), "w") as f:
        emit_compressed(f, "mspacman_pacrom", "unsigned char", "", 0x4000, list(drom[0:0x4000]))

    with open(os.path.join(OUT_DIR, "mspacman_auxrom.h"), "w") as f:
        emit_compressed(f, "mspacman_auxrom", "unsigned char", "", 0x2000, list(drom[0x8000:0xA000]))

    print(f"\nCompletato con successo.")

if __name__ == "__main__":
    main()