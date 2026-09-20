# mos6502.patch

Applied by `m6502patch.py` after unpacking + renaming
`romszip/M6502-081707.zip` into `source/src/cpus/mos6502/`, via
`romconv/pypatch.py` (`patch-ng`, no system `patch` binary needed).

## Regenerating after a ZIP update

1. Extract pristine `Codes.h`/`M6502.c`/`M6502.h`/`Tables.h` from the new
   ZIP into a dir `a/`, renamed to `Codes6502.h`/`M6502.c`/`M6502.h`/
   `Tables6502.h` (CRLF -> LF, unmodified).
2. Copy the current, already-patched files from
   `source/src/cpus/mos6502/` into a dir `b/`.
3. Re-apply the desired changes to the files in `b/`.
4. `diff -ruN a b > mos6502.patch`, then delete `a/`/`b/`. Pristine
   vendor source is not kept in the repo.
5. Verify: `patch -p1 -d a -i mos6502.patch` reproduces `b/` exactly.
