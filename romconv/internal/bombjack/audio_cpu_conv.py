#!/usr/bin/env python
import sys
import os

sys.path.insert(0, os.path.join("..", "pyconv"))
from asset_emit import emit_compressed

# --- Configurazione per Bomb Jack (Audio CPU) ---
# Nomi dei file ROM richiesti dal driver MAME (bombjack.cpp)
ROM_FILES = {
    "audio1": "../../roms/01_h03t.bin"
}

# File di output
OUTPUT_FILE = "../../../source/src/machines/bombjack/bombjack_rom2.h"
# Nome dell'array C nel file di output
ROM_ID = "bombjack_rom_cpu2"
# Only the 8 KB chip is emitted. The rest of MAME's 16 KB "audiocpu" region
# is 0xFF fill, returned by bombjack.cpp's read handlers instead.
AUDIO_ROM_SIZE = 0x2000  # 8KB

# --- Funzione principale di conversione (invariata) ---

def create_output_file(rom_data, outfile, id_name):
    """Scrive i dati della ROM in un file header C."""
    print(f"Scrittura del file di output: {outfile}...")
    with open(outfile, "w") as of:
        of.write(f"// File generato automaticamente per {id_name}\n")
        of.write(f"// Contiene la ROM della CPU Audio di Bomb Jack\n")
        emit_compressed(of, id_name, "unsigned char", "", len(rom_data), list(rom_data))
    print("Completato.")

def convert_bombjack_audio_rom():
    """
    Legge il file ROM della CPU audio di Bomb Jack, lo mappa correttamente
    e genera il file header C.
    """
    # Controlla se il file ROM esiste nella directory corrente
    print("Controllo del file ROM necessario per l'Audio CPU di Bomb Jack...")
    rom_path = "." # Si aspetta il file nella stessa directory dello script
    for fname in ROM_FILES.values():
        if not os.path.exists(os.path.join(rom_path, fname)):
            print(f"ERRORE: File ROM non trovato: '{fname}'")
            print("Assicurati che il file ROM sia nella stessa directory dello script.")
            sys.exit(1)
    print("Il file ROM dell'Audio CPU è stato trovato.")

    audio_memory = bytearray([0xFF] * AUDIO_ROM_SIZE)
    
    print("Inizio della mappatura della ROM in memoria...")

    # Mappatura basata sul driver MAME (bombjack.cpp)
    try:
        # ROM_LOAD( "01_h03t.3h", 0x0000, 0x2000, ... )
        with open(os.path.join(rom_path, ROM_FILES["audio1"]), "rb") as f:
            rom_data = f.read()
        assert len(rom_data) == AUDIO_ROM_SIZE

        # Inserisce la ROM all'inizio del buffer di memoria
        audio_memory[0x0000:AUDIO_ROM_SIZE] = rom_data
        print(f"- {ROM_FILES['audio1']} mappato correttamente a 0x0000.")

        final_rom_data = audio_memory

        # Genera il file di output
        create_output_file(final_rom_data, OUTPUT_FILE, ROM_ID)

    except FileNotFoundError as e:
        print(f"ERRORE: File non trovato - {e}")
        sys.exit(1)
    except Exception as e:
        print(f"Si è verificato un errore imprevisto: {e}")
        sys.exit(1)


if __name__ == "__main__":
    convert_bombjack_audio_rom()