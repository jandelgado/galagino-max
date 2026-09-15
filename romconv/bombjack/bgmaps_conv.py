#!/usr/bin/env python
import sys
import os

sys.path.insert(0, os.path.join("..", "pyconv"))
from romdata_emit import emit_compressed

# --- Configurazione per la Mappa del Background di Bomb Jack ---
INPUT_ROM_FILE = "../roms/02_p04t.bin"
OUTPUT_HEADER_FILE = "../../source/src/machines/bombjack/bombjack_bg_maps.h"
OUTPUT_ARRAY_NAME = "bombjack_bg_maps"

def create_output_file(rom_data, outfile, id_name):
    """Scrive i dati della ROM in un file header C."""
    print(f"Scrittura del file di output: {outfile}...")
    with open(outfile, "w") as of:
        of.write(f"// File generato automaticamente per la mappa del background di Bomb Jack.\n")
        of.write(f"// Dati estratti da: {INPUT_ROM_FILE}\n\n")
        emit_compressed(of, id_name, "unsigned char", "", len(rom_data), list(rom_data))
    print("Completato.")

def convert_bg_maps():
    """
    Legge il file ROM della mappa di background e genera il file header C.
    """
    print("--- Conversione Mappa Background per Bomb Jack ---")
    if not os.path.exists(INPUT_ROM_FILE):
        print(f"ERRORE: File ROM non trovato: '{INPUT_ROM_FILE}'")
        sys.exit(1)
    
    print(f"File ROM '{INPUT_ROM_FILE}' trovato.")

    try:
        with open(INPUT_ROM_FILE, "rb") as f:
            rom_data = f.read()
        
        # Il driver MAME fa un RELOAD, che duplica i dati.
        # La ROM è 0x1000 (4KB), ma la regione è 0x2000 (8KB).
        # Duplichiamo i dati per simulare il reload.
        final_rom_data = rom_data + rom_data
        print(f"Dati duplicati per simulare ROM_RELOAD. Dimensione finale: {len(final_rom_data)} bytes.")

        create_output_file(final_rom_data, OUTPUT_HEADER_FILE, OUTPUT_ARRAY_NAME)

    except Exception as e:
        print(f"Si è verificato un errore imprevisto: {e}")
        sys.exit(1)

if __name__ == "__main__":
    convert_bg_maps()