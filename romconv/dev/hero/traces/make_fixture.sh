#!/usr/bin/env bash
# Regenerates source/test/test_hero_game/hero_traces.h from the traces here.
#   make_fixture.sh <H.E.R.O..A26>
# name:script:MAME frames[:skip records after the glide start | :@record]
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
cd "$here"
PYTHONPATH=../../.. uv run --project ../../.. python -m dev.hero.trace_fixture "$1" \
  ../../../../source/test/test_hero_game/hero_traces.h \
  walk_wall_fly:walk_wall_fly.txt:520 \
  shaft_down_up:shaft_down_up.txt:700:5 \
  level1_dynamite:level1_dynamite.txt:1300 \
  level1_laser_rescue:level1_laser_rescue.txt:1300 \
  level10_critters_dark:level10_critters_dark.txt:600:20 \
  level10_raft:level10_raft.txt:700:20 \
  level17_tentacle:level17_tentacle.txt:800:20 \
  level14_moving_wall:level14_moving_wall.txt:700:20 \
  power_out:power_out.txt:600:20 \
  game_over:game_over.txt:400:20 \
  million:million.txt:300:20 \
  attract:attract.txt:3000:@2 \
  select_start:select_start.txt:600:@2
