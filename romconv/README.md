# romconv - ROM/bin file conversion

Arcade emulation involves dealing with the original ROM files of the
machines to be emulated. Sometimes the conversion is limited to the
transcription from binary to an equivalent C source file. But in many
cases the conversion includes further data processing. E.g. all color
tables are converted into the 16 bit color format used by the ILI9341
or ST7789 displays. Sprite and tile data is converted into a format
easier to process on the ESP32.

The conversion could be done on the ESP32 target at run time. But in
Galagino it's done beforehand. This offloads these tasks from the
ESP32.

It's possible to implement only one or two of all arcade
machines. In that case the related ROM conversion can be omitted and
the machine in question has to be disabled in the file
[config.h](../source/src/config.h).

The necessary ROM files need be placed in the [romszip
directory](../romszip) before these scripts can be run.

The `logos` step generates the menu logos (`*_logo.h`) from
[logos/](../logos).

## Do-it-all script

Running `convert.bat` (Windows) or `convert.sh` (Linux/macOS) with no
arguments converts all games. Both need [uv](https://docs.astral.sh/uv/)
on `PATH`; it installs the pinned Python and dependencies from
`pyproject.toml` automatically.

## Do it step by step

Pass one or more names to convert only those, `-j N` sets the number of
worker processes (default: CPU count, `-j1` runs everything in one process):

```
convert.bat [-j N] z80 galaga pacman
convert.sh [-j N] z80 galaga pacman
```

Names are the job names of [romconv/registry.py](romconv/registry.py): the
games (`1942`, `btime`, `invaders`, ...) and `logos`, `z80`, `m6502`,
`pokey_poly`, `unpack_fixture`, `namco54`, `williams_palette`,
`system1_palette`, `catalog`, `bundles`. A game that needs a family's
shared output (`galaga` -> `namco54`, a Williams game -> `williams_palette`,
a System 1 game -> `system1_palette`) pulls that job in; the utility jobs and
`catalog` always run, `bundles` only builds the bundles of the named games.

A ROM that is missing from its zip in [romszip](../romszip) (or has the wrong
SHA-1) fails only its own job with a message naming zip and file; the other
jobs finish, all logs are printed and the exit code is 1.

## Layout

`convert.py`/`.sh`/`.bat` are the only scripts meant to be run directly.

```
romconv/
  convert.py convert.sh convert.bat pyproject.toml uv.lock README.md
  romconv/        (a) the conversion the build needs; imports nothing from dev or tests
    core/         Paths, Job, Context, HeaderWriter, Manifest, runner, ConversionError
    emit/         asset headers (emit_compressed/emit_plain), LZMA, hex text
    plan/         Arena layout and plan solver
    remote/       bundle format, `bundles` and `catalog` jobs
    gfx/          generic converters (tiles, sprites, cmap, rom, wavetable, sample,
                  logo, adpcm), colour helpers, MAME gfx decode
    hw/           board families shared by several games (galaxian, namco, mcr, ...)
    cpu/          `z80` and `m6502` jobs (vendor zip + patch)
    misc/         `pokey_poly`, `unpack_fixture`
    games/        one module (or package) per machine, e.g. pacman.py, marble/
    registry.py   every job, in one list
  dev/            (b) dev tools: previews, MAME fixtures and traces, checks, golden diff
  tests/          (c) pytest tests of (a)
  build/          ignored: asset manifests, scratch dirs, previews, golden snapshots
```

Rules for (a), checked by `tests/test_contracts.py`: a converter gets
everything through its `Context` (ROMs via `ctx.rom(name, sha1)` from the
zips it declares, output via `ctx.header(...)`, log via `ctx.log`), raises
`ConversionError` on error, and uses no `sys.exit`, environment variables,
`os.chdir`, `sys.path` changes, `print` to stdout, mutable module state or
import-time work. A name is public only if another module imports it, all
others start with `_`.

### Adding a game

1. Write `romconv/games/<machine>.py` with a `_convert(ctx)` function and
   `JOB = Job(name="<name>", convert=_convert, machine="<machine dir>",
   zips=("<rom>.zip",), plan=True|False, deps=(...))`.
2. Add `<module>.JOB` to `ALL` in `romconv/registry.py`.
3. Add a test in `tests/` for non-trivial logic, `uv run pytest`.

### Tests

```
uv run pytest             # everything
uv run pytest -m "not slow"   # without the tests that run real conversions
```

`tests/test_gfx.py` compares real game jobs with the golden baseline
`build/golden/base` (ignored, so absent on a fresh clone): those tests are
skipped until it exists. Create it with `uv run python -m dev.golden run base`.

### Dev tools

Run from `romconv/` as `uv run python -m dev.<game>.<tool>`; they may import
`romconv`. Examples: `dev.marble.pf_check`, `dev.hero.trace_run`,
`dev.bombjack.preview_sprites` (images only under `build/preview/<game>/`),
`dev.tileaddr` (regenerates the tracked `source/src/machines/tileaddr.*`).
Tools that write tracked files (MAME fixtures, traces under `source/test`)
keep their targets.

`dev.golden` snapshots all generated output and diffs two snapshots; use it
to prove a change did not alter the output:

```
uv run python -m dev.golden run before      # cleans generated files, runs ./convert.py
# ... change code ...
uv run python -m dev.golden run after       # `-- -j1 galaga` passes arguments to convert.py
uv run python -m dev.golden diff before after   # "identical" or the differing files
```

## Asset split per Arena layout

Machines unpack COMPRESSED assets into the Arena, two RAM blocks whose
sizes depend on the build (`source/src/emulation/arena_layout.h`: block A
is smaller in `ASSETS_REMOTE` builds, which link WiFi). For games that do
not fit both layouts with one hand-written split, romconv computes the
split per layout:

- A job with `plan=True` makes every asset its converters emit carry both
  forms (flash array and LZMA blob) behind `#if <name>_PLAIN`, and registers
  the asset in the job's manifest.
- `romconv/plan/arena.py` runs last in the job. It reads the manifest,
  the block sizes and the machine header's `ARENA_EXTRA` (other
  `Arena::alloc` calls, in run-time order), `ARENA_EXTRA_ANY_ORDER`,
  `ARENA_RAM` and `ARENA_FLASH` (pins), all plain literals. It writes
  `<game>_arena_plan.h`: per layout the mode and Arena block of each asset,
  the `<name>_asset` type the machine declares its members with, and the
  bytes `Arena::reserve()` keeps for the planned assets.
- The converter's own choice (`emit_plain` vs `emit_compressed`) is kept
  whenever it fits; otherwise the split with the fewest PLAIN bytes wins.
- Changing `arena_layout.h`, or a machine's `ARENA_EXTRA` /
  `ARENA_EXTRA_ANY_ORDER` (the machine asserts `<game>_arena_extras_key`),
  makes the build fail until the game is converted again.
- `emit_*(..., plan=False)` keeps an asset out of the plan (e.g. ADPCM
  samples, whose count is not a byte count, or assets outside the game's
  directory).

## Remote bundles (mode 3, `ASSETS_REMOTE`)

A mode 3 build keeps the games of its env's `ENABLE_<X>` list in the app
and downloads every other catalog game from galagino-server. After the
per-game jobs, two more jobs run (phase 2):

- `romconv/remote/catalog.py` (job `catalog`) writes `source/src/catalog.h` (git-ignored):
  `GAME_<X>` (machine code compiled in: `ENABLE_<X>`, or any catalog game in
  an `ASSETS_REMOTE` build) and `BUILTIN_<DIR>` (assets of
  `machines/<dir>/` in the app; a built-in subclass keeps its base game's
  assets, e.g. `ENABLE_MSPACMAN` keeps pacman's). The catalog is every game
  in `machines.h` except the broken ones (`EXCLUDED`).
- `romconv/remote/bundles.py` (job `bundles`) builds one bundle per
  requested catalog game (all of them in a full run) from the manifests the
  jobs persist in `build/assets/<dir>/` (`<name>.json`, `.raw`,
  `.lz` full dictionary, `.lz64` 64 KB dictionary); a missing dir is
  an error. It writes `galagino-server/data/g/<hash>` and
  `<game>_bundle.h` (`BundleRef`: hash, wire and cache entry length, name)
  and prints, per game, wire and cache entry bytes and what the 64 KB
  dictionary costs over a full one.

Every asset header emitted into a machine dir carries a remote blob form
behind `#if defined(ASSETS_REMOTE) && !defined(BUILTIN_<DIR>)`. Logos and
the shared dirs (`namco54`, `williams`) stay built in. In the bundle,
PLAIN assets are LZMA with a 64 KB dictionary (the device decodes them while
downloading, into raw flash), COMPRESSED assets carry the app's own stream.
Bundle layout: `romconv/remote/bundle.py`.
