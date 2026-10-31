@echo off
rem PlatformIO, pinned, run by uv: the Windows twin of tools/pio
uvx --from platformio==6.1.19 --with intelhex pio %*
