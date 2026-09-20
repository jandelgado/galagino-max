#!/usr/bin/env python
from PIL import Image, ImageDraw, ImageFont

from tiles_bg_conv import decode_tiles, dump_row_to_ulong_pair, NUM_TILES, TILE_WIDTH, TILE_HEIGHT

# --- CONFIGURAZIONE ---
OUTPUT_IMAGE_FILE = "bombjack_bg_preview.png"
TILES_PER_ROW = 16

def decode():
    chars = decode_tiles()
    tile_data_flat = []
    for c in chars:
        for y in range(TILE_HEIGHT):
            val1, val2 = dump_row_to_ulong_pair(c[y])
            tile_data_flat.append(val1)
            tile_data_flat.append(val2)
    return tile_data_flat

def create_tile_preview(tile_data_flat):
    preview_palette = { 0: (0,0,0), 1: (255,85,85), 2: (85,255,85), 3: (255,255,85), 4: (85,85,255), 5: (255,85,255), 6: (85,255,255), 7: (255,255,255) }
    num_rows = (NUM_TILES + TILES_PER_ROW - 1) // TILES_PER_ROW
    cell_w, cell_h = TILE_WIDTH + 4, TILE_HEIGHT + 14
    img = Image.new('RGB', (cell_w * TILES_PER_ROW, cell_h * num_rows), (30, 30, 30))
    draw = ImageDraw.Draw(img)
    try: font = ImageFont.truetype("arial.ttf", 10)
    except IOError: font = ImageFont.load_default()

    for idx in range(NUM_TILES):
        row_idx, col_idx = divmod(idx, TILES_PER_ROW)
        x_offset, y_offset = col_idx * cell_w + 2, row_idx * cell_h + 2
        for y in range(TILE_HEIGHT):
            base_idx = idx * (TILE_HEIGHT * 2) + (y * 2)
            packed_left = tile_data_flat[base_idx]
            packed_right = tile_data_flat[base_idx + 1]

            for x in range(8):
                pixel_value = (packed_left >> (3 * (7 - x))) & 0x07
                img.putpixel((x_offset + x, y_offset + y), preview_palette.get(pixel_value, (0,0,0)))
            for x in range(8):
                pixel_value = (packed_right >> (3 * (7 - x))) & 0x07
                img.putpixel((x_offset + 8 + x, y_offset + y), preview_palette.get(pixel_value, (0,0,0)))

        draw.text((x_offset, y_offset + TILE_HEIGHT), f"{idx}", font=font, fill=(200, 200, 200))
        draw.rectangle([x_offset -1, y_offset -1, x_offset + TILE_WIDTH, y_offset + TILE_HEIGHT], outline=(80, 80, 80))

    img.save(OUTPUT_IMAGE_FILE)
    print(f"\nImmagine di anteprima salvata come '{OUTPUT_IMAGE_FILE}'")

if __name__ == "__main__":
    create_tile_preview(decode())
