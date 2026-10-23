# Samples

Some arcade machines contained special hardware to generate certain
sounds. Emulating this hardware may be tricky and the flash memory
size of the ESP32 allows to store some of these as digital sound
samples.

This directory contains samples for Donkey Kong.

The `dkong` conversion job (`romconv/convert.sh dkong`) turns them into
IMA-ADPCM C headers (`source/src/machines/dkong/dkong_sample_*.h`) with
[sample.py](../romconv/romconv/gfx/sample.py).

Donkey Kong does most audio via its MB8884/I8048 audio CPU. But three
sounds are generated via discrete logic including three variants of
the walk sound. These are still regenerated using samples.
