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

The logo conversion for the game selection menu might require the
seperate installation of the ```imageio python module``` which can
e.g. be done by the following command. This is usually not needed as
the logos are included pre-converted. This is only needed if you intend
the change the logos.

```pip3 install imageio```

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