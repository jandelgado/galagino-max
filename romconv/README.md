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

Pass one or more game names to convert only those:

```
convert.bat z80 galaga pacman
convert.sh z80 galaga pacman
```

Game names match the `internal/pyconv/conv_<name>.py` scripts.

## Layout

`convert.py`/`.sh`/`.bat` are the only scripts meant to be run
directly. Everything else (`internal/`) is implementation detail:
per-game converters, shared helpers, and unpacked ROM working data.

## Asset split per Arena layout

Machines unpack COMPRESSED assets into the Arena, two RAM blocks whose
sizes depend on the build (`source/src/emulation/arena_layout.h`: block A
is smaller in `ASSETS_REMOTE` builds, which link WiFi). For games that do
not fit both layouts with one hand-written split, romconv computes the
split per layout:

- `conv_<game>.py` sets `ROMCONV_PLAN=<game>`. Every asset the converters
  emit then carries both forms (flash array and LZMA blob) behind
  `#if <name>_PLAIN`, plus a `// arena-asset` marker.
- `internal/pyconv/arena_plan.py <game>` runs last. It reads the markers,
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
per-game converters, `convert.py` runs two more steps:

- `internal/pyconv/catalog.py` writes `source/src/catalog.h` (git-ignored):
  `GAME_<X>` (machine code compiled in: `ENABLE_<X>`, or any catalog game in
  an `ASSETS_REMOTE` build) and `BUILTIN_<DIR>` (assets of
  `machines/<dir>/` in the app; a built-in subclass keeps its base game's
  assets, e.g. `ENABLE_MSPACMAN` keeps pacman's). The catalog is every game
  in `machines.h` except the broken ones (`EXCLUDED`).
- `internal/pyconv/bundles.py [game ...]` builds one bundle per catalog
  game from the sidecars `asset_emit.py` leaves in `build/assets/<dir>/`
  (`<name>.json`, `.raw`, `.lz` full dictionary, `.lz64` 64 KB
  dictionary). It writes `galagino-server/data/g/<hash>` and
  `<game>_bundle.h` (`BundleRef`: hash, wire and cache entry length, name)
  and prints, per game, wire and cache entry bytes and what the 64 KB
  dictionary costs over a full one.

Every asset header emitted into a machine dir carries a remote blob form
behind `#if defined(ASSETS_REMOTE) && !defined(BUILTIN_<DIR>)`. Logos and
the shared dirs (`namco54`, `williams`) stay built in. In the bundle,
PLAIN assets are LZMA with a 64 KB dictionary (the device decodes them while
downloading, into raw flash), COMPRESSED assets carry the app's own stream.
Bundle layout: `internal/pyconv/bundle.py`.
