#!/usr/bin/env python3
# Checks arena_plan.py: Arena split solver, machine header parse, plan header.
import os, random, tempfile, time
from arena_plan import solve, plan_header, parse_machine_header, parse_markers

W = [49152, 35040, 35040]  # Williams RAM + 2 scanout buffers
REMOTE, LOCAL = (69000, 110500), (91000, 110500)

# 1. defender: placement alone fits, nothing PLAIN
m, r = solve([("d_main", 12288, 1), ("d_banked", 28672, 1), ("d_sound", 2048, 1)], W, REMOTE)
assert "PLAIN" not in m.values(), m
print("1. defender fits by placement: OK")

# 2. joust: no block split works, the main ROM goes to flash; local fits
J = [("j_main", 49152, 1), ("j_sound", 4096, 1)]
m, r = solve(J, W, REMOTE)
assert [n for n, v in m.items() if v == "PLAIN"] == ["j_main"], m
m, r = solve(J, W, LOCAL)
assert "PLAIN" not in m.values(), m
print("2. joust main ROM PLAIN remote, none local: OK")

# 3. pins
m, r = solve([("a", 10, 1)], [], REMOTE, flash=["a"])
assert m["a"] == "PLAIN"
try:
    solve(J, W, REMOTE, ram=["j_main"])
    raise AssertionError("infeasible pin accepted")
except ValueError:
    pass
try:
    solve([("a", 10, 1)], [], REMOTE, ram=["nope"])
    raise AssertionError("unknown pin accepted")
except ValueError:
    pass
print("3. pins: OK")

# 4. reservation = planned bytes per block, rounded to 4
m, r = solve([("x", 5, 1)], [], REMOTE)
assert r in ((8, 0), (0, 8)), r
assert m["x"] in ("A", "B")
print("4. reservation rounded: OK")

# 5. an asset larger than both blocks is PLAIN, not an error
m, r = solve([("big", 205664, 4), ("s", 100, 1)], [], REMOTE)
assert m["big"] == "PLAIN" and m["s"] != "PLAIN"
print("5. oversize asset PLAIN: OK")

# 6. plan header
h = plan_header("1942", {"local": ({"x": "A"}, (8, 0)), "remote": ({"x": "PLAIN"}, (0, 0))},
                {"x": "unsigned char[8][8]"})
assert '#include "../../emulation/asset.h"' in h
assert "using x_asset = Asset<unsigned char[8][8], PLAIN>;" in h
assert "using x_asset = Asset<unsigned char[8][8], COMPRESSED>;" in h
assert "#ifndef _1942_ARENA_PLAN_H" in h
assert "static constexpr ArenaPlan _1942_arena_plan = { 0, 0 };" in h
assert "static constexpr ArenaPlan _1942_arena_plan = { 8, 0 };" in h
assert "#define x_PLAIN 1" in h and "#define x_BLOCK ARENA_ANY" in h
assert "#define x_PLAIN 0" in h and "#define x_BLOCK ARENA_A" in h
assert "Arena::BLOCK_A_SIZE == 69000 && Arena::BLOCK_B_SIZE == 110500" in h
assert "Arena::BLOCK_A_SIZE == 91000 && Arena::BLOCK_B_SIZE == 110500" in h
assert h.index("#ifdef ASSETS_REMOTE") < h.index("{ 0, 0 }") < h.index("#else") < h.index("{ 8, 0 }")
print("6. plan header: OK")

# 7. machine header parse
d = tempfile.mkdtemp()
p = os.path.join(d, "g.h")
with open(p, "w") as f:
    f.write("class g {\n  static constexpr uint32_t ARENA_EXTRA[] = { 49152, 35040,\n    35040 };  // hw\n"
            '  static constexpr const char *ARENA_RAM[] = { "a", "b" };\n};\n')
assert parse_machine_header(p) == ([49152, 35040, 35040], ["a", "b"], [])
with open(p, "w") as f:
    f.write("class g {\n};\n")
assert parse_machine_header(p) == ([], [], [])
with open(p, "w") as f:
    f.write("static constexpr uint32_t ARENA_EXTRA[] = { WilliamsBoard::RAM_SIZE };\n")
try:
    parse_machine_header(p)
    raise AssertionError("expression accepted")
except ValueError:
    pass
print("7. machine header parse: OK")

# 8. markers
with open(os.path.join(d, "a.h"), "w") as f:
    f.write("// arena-asset g_rom 16384 1 PLAIN unsigned char[8][8]\n#if g_rom_PLAIN\n")
with open(os.path.join(d, "g_arena_plan.h"), "w") as f:
    f.write("// arena-asset stale 1 1\n")
assert parse_markers(d) == [("g_rom", 16384, 1, "PLAIN", "unsigned char[8][8]")]
print("8. markers: OK")

# 8b. extras must fit in every order (function arguments: unspecified order)
try:
    solve([], [100, 60], (100, 60))
    raise AssertionError("order-dependent fit accepted")
except ValueError:
    pass
print("8b. extras fit in any order: OK")

# 8c. equal PLAIN bytes: the converter's mode (hint) wins over names
A = [("a_tiles", 8192, 1, "COMPRESSED"), ("b_rom", 8192, 1, "PLAIN"), ("c", 60000, 1, "COMPRESSED")]
m, r = solve(A, [], (60000, 8192))
assert m["b_rom"] == "PLAIN" and m["a_tiles"] != "PLAIN", m
print("8c. tie-break by converter mode: OK")

# 9. 16 assets solve in reasonable time
rnd = random.Random(1)
assets = [(f"a{i}", rnd.choice([4096, 8192, 16384, 32768, 65536]) + rnd.randrange(0, 4096), 1)
          for i in range(16)]
t0 = time.time()
solve(assets, [4096], REMOTE)
dt = time.time() - t0
print(f"9. 16 assets in {dt:.2f} s")
assert dt < 5
