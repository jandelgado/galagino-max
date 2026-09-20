#!/usr/bin/env python
import sys
from PIL import Image, ImageDraw, ImageFont

# --- Configurazione ---

OUTPUT_IMAGE_FILE = "dkongjr_tileset.png"

PALETTE = {
    0: (20, 20, 40),
    1: (255, 80, 80),
    2: (80, 255, 80),
    3: (255, 255, 80)
}

TILE_SIZE = 8
TILE_ZOOM = 6
TILES_PER_ROW = 32
PADDING = 8
LABEL_HEIGHT = 30

def visualize_tiles_as_image(all_tiles_data):
    """Crea un'immagine PNG con tutti i tile, ingranditi e con etichette leggibili."""
    if not all_tiles_data:
        print("ERRORE: Nessun dato da visualizzare.")
        sys.exit(1)

    num_tiles = len(all_tiles_data)
    num_rows = (num_tiles + TILES_PER_ROW - 1) // TILES_PER_ROW

    cell_width = TILE_SIZE * TILE_ZOOM + PADDING
    cell_height = TILE_SIZE * TILE_ZOOM + PADDING + LABEL_HEIGHT

    img_width = cell_width * TILES_PER_ROW
    img_height = cell_height * num_rows
    
    image = Image.new('RGB', (img_width, img_height), (20, 20, 40))
    draw = ImageDraw.Draw(image)
    
    try:
        font = ImageFont.truetype("cour.ttf", 15)
    except IOError:
        print("Font 'cour.ttf' non trovato, uso il font di default.")
        font = ImageFont.load_default()

    print("Disegno dei tile nell'immagine...")
    for i, tile_data in enumerate(all_tiles_data):
        row = i // TILES_PER_ROW
        col = i % TILES_PER_ROW
        
        start_x = col * cell_width + PADDING // 2
        start_y = row * cell_height + PADDING // 2

        for y in range(TILE_SIZE):
            pix_row_data = tile_data[y]
            for x in range(TILE_SIZE):
                color_index = (pix_row_data >> (14 - x * 2)) & 3
                color_rgb = PALETTE.get(color_index)
                
                draw.rectangle(
                    [
                        (start_x + x * TILE_ZOOM, start_y + y * TILE_ZOOM),
                        (start_x + (x + 1) * TILE_ZOOM - 1, start_y + (y + 1) * TILE_ZOOM - 1)
                    ],
                    fill=color_rgb
                )

        # --- LOGICA DI CENTRATURA MANUALE DEL TESTO (CORREZIONE) ---
        label1 = f"#{i:03d}"
        label2 = f"(0x{i:X})"
        
        # Posizione Y per le etichette
        label1_y = start_y + TILE_SIZE * TILE_ZOOM + 2
        label2_y = label1_y + 14 # Spazio tra le due righe di testo
        
        # Calcola la larghezza di ogni etichetta per centrarla
        # NOTA: textbbox è disponibile nelle versioni più recenti di Pillow. 
        # Se anche questo dà errore, useremo un metodo più vecchio.
        try:
             bbox1 = draw.textbbox((0, 0), label1, font=font)
             bbox2 = draw.textbbox((0, 0), label2, font=font)
             text_width1 = bbox1[2] - bbox1[0]
             text_width2 = bbox2[2] - bbox2[0]
        except AttributeError: # Fallback per versioni molto vecchie di Pillow
             text_width1 = len(label1) * 8 # Stima approssimativa
             text_width2 = len(label2) * 8 # Stima approssimativa


        # Posizione X centrata per ogni etichetta
        label1_x = start_x + (TILE_SIZE * TILE_ZOOM - text_width1) / 2
        label2_x = start_x + (TILE_SIZE * TILE_ZOOM - text_width2) / 2

        draw.text((label1_x, label1_y), label1, fill=(220, 220, 220), font=font)
        draw.text((label2_x, label2_y), label2, fill=(200, 200, 150), font=font)

    image.save(OUTPUT_IMAGE_FILE)
    print(f"\nImmagine salvata con successo come '{OUTPUT_IMAGE_FILE}'")