"""Marble Madness (Atari, 1984; MAME atarisy1.cpp, marble): playfield, physics, sprites, HUD, sound."""
from romconv.core.job import Job
from romconv.games.marble import convert as _conversion

JOB = Job(name="marble", convert=_conversion.convert, machine="marble", zips=("marble.zip", "atarisy1.zip"),
          plan=True)
