#!/usr/bin/env bash
# Headless MAME a2600 with harness.lua.
#   run_mame.sh <rom.a26> <workdir>   env: H_SCRIPT H_FRAMES H_SNAP H_RAM H_READS (see harness.lua)
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
rom=$(realpath "$1")
cd "$2"
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy mame a2600 -cart "$rom" \
  -video none -sound none -nothrottle -noautosave -skip_gameinfo -snapshot_directory "$PWD" \
  -autoboot_script "$here/harness.lua" 2>&1 | grep -v -e osd_subst -e ALSA -e 'video none' || true
