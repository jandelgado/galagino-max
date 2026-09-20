#!/usr/bin/env python
import sys
import os

sys.path.insert(0, os.path.join("..", "pyconv"))
from romdata_emit import emit_compressed

# --- Configurazione Specifica per Bomb Jack (Sprites) ---
INPUT_ROM_FILES = ["../../roms/16_m07b.bin", "../../roms/15_l07b.bin", "../../roms/14_j07b.bin"]
OUTPUT_HEADER_FILE = "../../../source/src/machines/bombjack/bombjack_sprites.h"

# Imposta a False per usare il tuo blitter che fa la rotazione al volo.
# Imposta a True se vuoi che lo script ruoti i dati e il tuo blitter li disegni direttamente.
ROTATE_SPRITES = False 

def rotate_matrix_90_cw(matrix):
    """Ruota una matrice 2D di 90 gradi in senso orario."""
    h, w = len(matrix), len(matrix[0])
    return [[matrix[h - 1 - x][y] for x in range(h)] for y in range(w)]

def decode_sprite(rom_planes, width, height, base_offset):
    """
    Decodifica un singolo sprite, simulando la logica di GATHER di floooh.
    """
    sprite_matrix = [[0] * width for _ in range(height)]
    
    # Simula il contatore 'off' del codice C
    off = base_offset
    
    for y in range(height):
        # Simula le macro GATHER
        if width == 16:
            bm0 = (rom_planes[2][off] << 8) | rom_planes[2][off + 8]
            bm1 = (rom_planes[1][off] << 8) | rom_planes[1][off + 8]
            bm2 = (rom_planes[0][off] << 8) | rom_planes[0][off + 8]
        elif width == 32:
            bm0 = (rom_planes[2][off] << 24) | (rom_planes[2][off+8] << 16) | (rom_planes[2][off+32] << 8) | rom_planes[2][off+40]
            bm1 = (rom_planes[1][off] << 24) | (rom_planes[1][off+8] << 16) | (rom_planes[1][off+32] << 8) | rom_planes[1][off+40]
            bm2 = (rom_planes[0][off] << 24) | (rom_planes[0][off+8] << 16) | (rom_planes[0][off+32] << 8) | rom_planes[0][off+40]
        else:
            raise ValueError("Dimensione non supportata")

        # Estrai i pixel per la riga corrente
        for x in range(width):
            bit_pos = width - 1 - x
            pen = (((bm2 >> bit_pos) & 1) << 0) | \
                  (((bm1 >> bit_pos) & 1) << 1) | \
                  (((bm0 >> bit_pos) & 1) << 2)
            sprite_matrix[y][x] = pen
            
        # Aggiorna l'offset 'off' come farebbe il codice C
        off += 1
        if width == 16:
            if y == 7: off += 8
        elif width == 32:
            if (y & 7) == 7: off += 8
            if (y & 15) == 15: off += 32
            
    return sprite_matrix

def dump_sprite_values(data):
    """Converte una matrice 2D di pixel in una lista di valori 'uint32_t'."""
    vals = []
    width = len(data[0])
    for y_row in data:
        # Impacchetta 8 pixel alla volta
        for chunk_start in range(0, width, 8):
            chunk = y_row[chunk_start : chunk_start + 8]
            val = 0
            for pixel in chunk:
                val = (val << 3) | pixel
            vals.append(val)
    return vals

def load_rom_planes():
    for filename in INPUT_ROM_FILES:
        if not os.path.exists(filename):
            print(f"ERRORE: File ROM non trovato: '{filename}'")
            sys.exit(1)

    # L'ordine è importante per la funzione di decodifica: LSB, bit1, MSB
    return [
        open(INPUT_ROM_FILES[2], "rb").read(), # 14_j07b.bin (LSB)
        open(INPUT_ROM_FILES[1], "rb").read(), # 15_l07b.bin (bit 1)
        open(INPUT_ROM_FILES[0], "rb").read()  # 16_m07b.bin (MSB)
    ]

def build_sprite_set(rom_data, width, height, num_sprites):
    """Decodifica un set di sprite in una lista flat di valori 'uint32_t'."""
    print(f"Processando {num_sprites} sprite {width}x{height}...")
    sprite_byte_size = (width * height) // 8

    flat = []
    for i in range(num_sprites):
        base_offset = i * sprite_byte_size
        sprite_matrix = decode_sprite(rom_data, width, height, base_offset)

        if ROTATE_SPRITES:
            sprite_matrix = rotate_matrix_90_cw(sprite_matrix)

        flat.extend(dump_sprite_values(sprite_matrix))

    return flat

def write_header(rom_data):
    with open(OUTPUT_HEADER_FILE, "w") as f:
        f.write(f"// File generato automaticamente per gli sprite di Bomb Jack.\n")
        f.write(f"// Logica di decodifica basata sull'emulatore di floooh.\n")
        if ROTATE_SPRITES:
            f.write(f"// Gli sprite sono stati ruotati di 90 gradi in senso orario.\n\n")
        else:
            f.write(f"// Gli sprite NON sono stati ruotati. Il blitter deve gestire la rotazione.\n\n")

        for width, height, num_sprites, array_name in [
            (16, 16, 256, "bombjack_sprites_16x16"),
            (32, 32, 64, "bombjack_sprites_32x32"),
        ]:
            flat = build_sprite_set(rom_data, width, height, num_sprites)
            emit_compressed(f, array_name, "uint32_t", "[%d]" % (width * height // 8), num_sprites, flat)

    print(f"\nProcesso completato! Il file '{OUTPUT_HEADER_FILE}' è stato creato.")

def main():
    print("--- Conversione Sprite per Bomb Jack (Logica Corretta) ---")
    rom_data = load_rom_planes()
    write_header(rom_data)

if __name__ == "__main__":
    main()