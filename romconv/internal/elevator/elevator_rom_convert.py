#!/usr/bin/env python3
# Elevator Action (Taito 1983), MAME taito/taitosj.cpp, set elevatorb:
# bootleg with the 68705 protection patched out. Files of romszip/elevator.zip.
# Main ROM: 0000-7fff, then bank 1 7000-7fff (ic52, MAME region 0x11000).
# Gfx ROM: read by the CPU through d404 and copied into character RAM, so
# tiles are decoded at render time, not here.
# Draw order: MAME taitosj_state::compute_draw_order() on the layer PROM,
# 32 priority values (d300 & 0x1f) x 4 planes back to front
# (0 = sprites, 1..3 = tilemaps).
import os
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join("..", "pyconv"))
from gfxutil import load_file
from asset_emit import emit_compressed

ROM_SET = os.path.normpath(os.path.join("..", "..", "..", "romszip", "elevator.zip"))
OUT_DIR = os.path.normpath(os.path.join("..", "..", "..", "source", "src", "machines", "elevator"))

if not os.path.isfile(ROM_SET):
    print(f"ERROR: missing {ROM_SET}", file=sys.stderr)
    sys.exit(1)
os.makedirs(OUT_DIR, exist_ok=True)


def rom(names, sha1):
    return load_file(ROM_SET, names, sha1)


main = (rom(["elevatorb/eabl_12.2732.ic69"], "fad660d2983daad478085be3b1a951b0864485dd") +
        rom(["elevatora/ea_13.2732.ic68"], "b72fea84f8322463ff224e3b06698a1ed7e305b7") +
        rom(["elevatora/ea_14.2732.ic67"], "11b68b89ab0f580bfe88047e59bd9bba237a2eb4") +
        rom(["elevatorb/eabl_15.2732.ic66"], "99f23d82d7866e4bca8f5a508e0913673d12489b") +
        rom(["elevatora/ea_16.2732.ic65"], "a7657d3d661421d1fca3b04e4025725272b77203") +
        rom(["elevatora/ea_17.2732.ic64"], "163daa07b6d45469f18e4f4a1904b60a890c8699") +
        rom(["elevatorb/eabl_18.2732.ic55"], "1de128cf96e6d976f6f09e4ac2b4d2507935bfe9") +
        rom(["elevatorb/eabl_19.2732.ic54"], "b2f684b61feb31e3d1856c16edcef33262f68581") +
        rom(["elevatorb/eabl.2732.ic52"], "e8980536f787fcb603943d4b2b3a64f475e51a16"))
sound = (rom(["ba3__09.2732.ic70", "ea_9.2732.ic70"], "abb916d675ee85032697d656121d4f525202cab3") +
         rom(["ba3__10.2732.ic71", "ea_10.2732.ic71"], "9970fba3afeaaaa7fd217f0704fb9df9cf13cf65"))
gfx = (rom(["elevatora/ea_20.2732.ic1"], "a8e3a0886ea5dc8e70aa280b4cef5fb26ca0e125") +
       rom(["elevatora/ea_21.2732.ic2"], "0ba292ac34dbf779a929db6358cd842d38077b3d") +
       rom(["elevatora/ea_22.2732.ic3"], "f1a18c09e31edb4ec3ad7ab853f425383ca22314") +
       rom(["elevatora/ea_23.2732.ic4"], "0767701213920d30d5a3a226b25cfbbd3f24437a") +
       rom(["elevatora/ea_24.2732.ic5"], "fe116c53a7e8ac523a17249a56df9f40b503b30d") +
       rom(["elevatora/ea_25.2732.ic6"], "646491c1d28904d9e662b1bff554bb74ec47708d") +
       rom(["elevatora/ea_26.2732.ic7"], "30bca7f5214bf424aa10184094947496f054ddf4") +
       rom(["elevatorb/eabl_27.2732.ic8"], "ec4db1392967f3959574bc4ca03e95938a6e5173"))
prom = rom(["eb16.ic22"], "d233f1bf8a3e6cd876853ffd721b9b64c61c9047")


def draw_order():
    order = []
    for i in range(32):
        mask, planes = 0, [0] * 4
        for j in range(3, -1, -1):
            data = prom[0x10 * (i & 0x0f) + mask] & 0x0f
            data = data >> 2 if i & 0x10 else data & 0x03
            mask |= 1 << data
            planes[j] = data
        order += planes
    return order


order = draw_order()
assert order[4 * 0x06:4 * 0x06 + 4] == [3, 2, 0, 1], order[24:28]   # attract: b m s f


def write(name, fn):
    path = os.path.join(OUT_DIR, name)
    with open(path, "w") as f:
        fn(f)
    print(f"wrote {path}")


write("elevator_main_rom.h", lambda f: emit_compressed(f, "elevator_main_rom", "unsigned char", "", len(main), main))
write("elevator_sound_rom.h", lambda f: emit_compressed(f, "elevator_sound_rom", "unsigned char", "", len(sound), sound))
write("elevator_gfx_rom.h", lambda f: emit_compressed(f, "elevator_gfx_rom", "unsigned char", "", len(gfx), gfx))
write("elevator_draw_order.h", lambda f: emit_compressed(f, "elevator_draw_order", "unsigned char", "", len(order), order))

CHARRAM = 0x3000   # Arena::alloc in the machine ctor
sizes = {"main": len(main), "sound": len(sound), "gfx": len(gfx), "order": len(order), "charram": CHARRAM}
for k, v in sizes.items():
    print(f"  {k:8s} {v:6d}")
total = sum(sizes.values())
print(f"Arena total {total} B (budget 182000)")
assert total < 182000
