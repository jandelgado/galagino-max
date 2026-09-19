#!/usr/bin/python3

import os
import zipfile

from pypatch import apply_patch

ZIPLOC = "../romszip/"
ZIP = "Z80-081707.zip"
DEST = "../source/src/cpus/z80/"
PATCH_FILE = "z80/z80.patch"

# Files copied verbatim from the vendor ZIP (CRLF normalized only).
# Z80.h / Z80.c additionally receive local modifications via PATCH_FILE.
FILES = ["CodesCB.h", "Codes.h", "CodesXX.h", "CodesED.h", "CodesXCB.h", "Tables.h", "Z80.h", "Z80.c"]

def unpack_z80(zip_path):
  with zipfile.ZipFile(zip_path, 'r') as zf:
    for name in FILES:
      print("Copying", name)
      code = zf.read("Z80/" + name).replace(b"\r\n", b"\n")
      with open(DEST + name, "wb") as of:
        of.write(code)

def main():
  os.makedirs(DEST, exist_ok=True)

  print(f"Load ZIP from: {os.path.abspath(ZIPLOC + ZIP)}")
  print(f"Target files:  {os.path.abspath(DEST)}")

  unpack_z80(ZIPLOC + ZIP)

  print("Patching Z80.h / Z80.c")
  apply_patch(PATCH_FILE, DEST)

if __name__ == "__main__":
    main()
