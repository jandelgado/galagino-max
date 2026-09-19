# z80.patch

Applied by `z80patch.py` after unpacking `romszip/Z80-081707.zip` into
`source/src/cpus/z80/`, via `romconv/pypatch.py` (`patch-ng`, no system
`patch` binary needed).

## Regenerating after a ZIP update

1. Extract pristine `Z80.h`/`Z80.c` from the new ZIP into a dir `a/`
   (CRLF -> LF, unmodified).
2. Copy the current, already-patched `Z80.h`/`Z80.c` from
   `source/src/cpus/z80/` into a dir `b/`.
3. Re-apply the desired changes to the files in `b/`.
4. `diff -ruN a b > z80.patch`, then delete `a/`/`b/`. Pristine vendor
   source is not kept in the repo.
5. Verify: `patch -p1 -d a -i z80.patch` reproduces `b/` exactly.
