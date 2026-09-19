#!/usr/bin/python3

import os
import zipfile

from pypatch import apply_patch

ZIP_FILE = os.path.normpath(os.path.join("..", "romszip", "M6502-081707.zip"))
DEST_DIR = os.path.normpath(os.path.join("..", "source", "src", "cpus", "mos6502"))
PATCH_FILE = os.path.normpath(os.path.join("mos6502", "mos6502.patch"))

COPY   = ["Codes.h",     "M6502.c", "M6502.h", "Tables.h"]
RENAME = ["Codes6502.h", "M6502.c", "M6502.h", "Tables6502.h"]

def unpack_6502(zip_path):
  with zipfile.ZipFile(zip_path, 'r') as zf:
    for src, dst in zip(COPY, RENAME):
      print("Copying", src, "\t->\t", dst)
      code = zf.read("M6502/" + src).replace(b"\r\n", b"\n")
      with open(os.path.join(DEST_DIR, dst), "wb") as of:
        of.write(code)

def main():
  os.makedirs(DEST_DIR, exist_ok=True)

  print(f"Load ZIP from: {os.path.abspath(ZIP_FILE)}")
  print(f"Target files:  {os.path.abspath(DEST_DIR)}")

  unpack_6502(ZIP_FILE)

  print("Patching", ", ".join(RENAME))
  apply_patch(PATCH_FILE, DEST_DIR)

if __name__ == "__main__":
    main()
