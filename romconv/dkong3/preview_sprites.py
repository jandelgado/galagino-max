import sys
from PIL import Image, ImageDraw, ImageFont

from sprites_conv import decode_sprites, dump_sprite_values
from cmap_conv import generate_master_palette

# --- CONFIGURAZIONE ---
OUTPUT_IMAGE_FILE = "spritesheet_preview.png"
SPRITES_PER_ROW = 16
SPRITE_WIDTH = 16
SPRITE_HEIGHT = 16

def create_preview(sprite_data_noflip, sprite_palette, num_sprites):
    try:
        # Crea l'immagine
        print("Creazione immagine di anteprima...")
        num_rows = num_sprites // SPRITES_PER_ROW
        cell_w, cell_h = SPRITE_WIDTH + 1, SPRITE_HEIGHT + 1 + 15
        img = Image.new('RGB', (cell_w * SPRITES_PER_ROW, cell_h * num_rows), (20, 20, 20))
        draw = ImageDraw.Draw(img)
        font = ImageFont.load_default()

        for idx in range(num_sprites):
            row_idx, col_idx = idx // SPRITES_PER_ROW, idx % SPRITES_PER_ROW
            x_offset, y_offset = col_idx * cell_w, row_idx * cell_h

            # Per l'anteprima, usiamo i primi 4 colori della palette degli sprite
            preview_palette = {
                0: sprite_palette[0],
                1: sprite_palette[1],
                2: sprite_palette[2],
                3: (20, 20, 20) # Trasparente
            }

            for y in range(SPRITE_HEIGHT):
                row_data = sprite_data_noflip[idx * SPRITE_HEIGHT + y]
                for x in range(SPRITE_WIDTH):
                    px_val = (row_data >> ((SPRITE_WIDTH - 1 - x) * 2)) & 0x03
                    if px_val != 3:
                        img.putpixel((x_offset + x, y_offset + y), preview_palette[px_val])

            draw.text((x_offset + 2, y_offset + SPRITE_HEIGHT + 2), str(idx), font=font, fill=(255, 255, 255))
            draw.rectangle([x_offset, y_offset, x_offset + SPRITE_WIDTH, y_offset + SPRITE_HEIGHT], outline=(100, 100, 100))

        img.save(OUTPUT_IMAGE_FILE)
        print(f"Immagine di anteprima salvata come '{OUTPUT_IMAGE_FILE}'")

    except Exception as e:
        print(f"ERRORE: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)

def decode():
    sprites = decode_sprites()
    # Nessun flip (flip_x=False, flip_y=False) -- corrisponde al primo blocco
    # scritto da sprites_conv.py.
    sprite_data_noflip = [v for s in sprites for v in dump_sprite_values(s, False, False)]

    # Usiamo la palette dedicata agli sprite (seconda metà della palette master).
    sprite_palette_raw = generate_master_palette()[256:]

    # Converti la palette in tuple (R,G,B)
    sprite_palette = []
    for val16 in sprite_palette_raw:
        rgb565 = ((val16 & 0xFF00) >> 8) | ((val16 & 0x00FF) << 8)
        r5 = (rgb565 >> 11) & 0x1F
        g6 = (rgb565 >> 5) & 0x3F
        b5 = rgb565 & 0x1F
        r8 = (r5 * 255 + 15) // 31
        g8 = (g6 * 255 + 31) // 63
        b8 = (b5 * 255 + 15) // 31
        sprite_palette.append((r8, g8, b8))

    print(f"Palette sprite con {len(sprite_palette)} colori caricata.")
    return sprite_data_noflip, sprite_palette, len(sprites)

if __name__ == "__main__":
    try:
        sprite_data_noflip, sprite_palette, num_sprites = decode()
    except Exception as e:
        print(f"ERRORE: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)
    create_preview(sprite_data_noflip, sprite_palette, num_sprites)
