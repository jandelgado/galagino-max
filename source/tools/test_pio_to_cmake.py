import json
import tempfile
import unittest
from pathlib import Path

from tools import pio_to_cmake as p

CONFIG = json.dumps([
    ["platformio", [["default_envs", "x"]]],
    ["arena", [["esp32_local", "118784 110500"], ["rp2350", "200000 0"], ["bad3", "1 2 3"], ["zero", "0 2"],
               ["lead", "0118784 110500"]]],
    ["env:nolayout", [["build_flags", ["-D HAL_ESP"]]]],
    ["env:x", [["build_flags", [
        "-D HAL_ESP", "-D ARDUINO_RUNNING_CORE=0", "-D BUILD_ENV_NAME=$PIOENV",
        "-D CORE_DEBUG_LEVEL=0", "-D BOOT_SELFTEST", "-fopt-info", "-fverbose-asm",
        "-fdump-tree-switchconv", "-flto", "-D TFT_SPICLK=40000000",
        "-D ENABLE_PACMAN", "-D ENABLE_GALAGA", "-D ENABLE_DKONG",
    ]], ["custom_arena_layout", "esp32_local"]]],
])


class ParseFlags(unittest.TestCase):
    def test_drops_arduino_only_and_non_defines(self):
        flags = p._parse_flags(CONFIG, "x")
        self.assertNotIn("ARDUINO_RUNNING_CORE=0", flags)
        self.assertNotIn("CORE_DEBUG_LEVEL=0", flags)
        self.assertFalse([f for f in flags if f.startswith("-")])
        self.assertIn("ENABLE_PACMAN", flags)
        self.assertIn("TFT_SPICLK=40000000", flags)

    def test_expands_env_name(self):
        self.assertIn("BUILD_ENV_NAME=x", p._parse_flags(CONFIG, "x"))

    def test_unknown_env(self):
        with self.assertRaises(SystemExit):
            p._parse_flags(CONFIG, "nope")

    def test_only_replaces_games(self):
        flags = p._only(p._parse_flags(CONFIG, "x"), ["pacman"])
        self.assertEqual([f for f in flags if f.startswith("ENABLE_")], ["ENABLE_PACMAN"])
        self.assertIn("BOOT_SELFTEST", flags)

    def test_hal_override(self):
        flags = p._hal(p._parse_flags(CONFIG, "x"), "rp2350")
        self.assertIn("HAL_RP2350", flags)
        self.assertNotIn("HAL_ESP", flags)


class LayoutDefines(unittest.TestCase):
    # the same three defines extraScript.py emits for a PlatformIO build
    def test_layout_defines_default(self):
        self.assertEqual(p._layout_defines(CONFIG, "x", None),
                         ["ARENA_LAYOUT_ESP32_LOCAL", "ARENA_A_SIZE=118784", "ARENA_B_SIZE=110500"])

    def test_layout_override(self):
        self.assertEqual(p._layout_defines(CONFIG, "x", "rp2350"),
                         ["ARENA_LAYOUT_RP2350", "ARENA_A_SIZE=200000", "ARENA_B_SIZE=0"])

    def test_unknown_layout(self):
        with self.assertRaises(SystemExit):
            p._layout_defines(CONFIG, "x", "nope")

    def test_bad_rows_rejected(self):
        # same rules as extraScript.py: two decimal tokens, A > 0
        for name in ("bad3", "zero"):
            with self.assertRaises(SystemExit):
                p._layout_defines(CONFIG, "x", name)

    def test_leading_zero_normalized(self):
        # "0118784" would be an octal C literal
        self.assertIn("ARENA_A_SIZE=118784", p._layout_defines(CONFIG, "x", "lead"))

    def test_env_without_layout(self):
        with self.assertRaises(SystemExit) as cm:
            p._layout_defines(CONFIG, "nolayout", None)
        self.assertIn("has no custom_arena_layout", str(cm.exception))


class Sources(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        root = Path(self.tmp.name)
        for f in ["main.cpp", "cpus/m6502/m6502.c", "net/loader.cpp", "hal/esp/audio.cpp",
                  "hal/rp2350/time.cpp", "hal/host/x.cpp", "hal/hal.h", "machines/a/a.cpp"]:
            (root / f).parent.mkdir(parents=True, exist_ok=True)
            (root / f).write_text("")
        self.root = root

    def tearDown(self):
        self.tmp.cleanup()

    def names(self, **kw):
        return [s for s in p._sources(self.root, **kw)]

    def test_selects_one_backend_and_no_net(self):
        got = self.names(hal="rp2350", net=False)
        self.assertIn("hal/rp2350/time.cpp", got)
        self.assertIn("main.cpp", got)
        self.assertIn("cpus/m6502/m6502.c", got)
        self.assertNotIn("hal/esp/audio.cpp", got)
        self.assertNotIn("hal/host/x.cpp", got)
        self.assertNotIn("net/loader.cpp", got)
        self.assertNotIn("hal/hal.h", got)

    def test_net_kept(self):
        self.assertIn("net/loader.cpp", self.names(hal="esp", net=True))


class Emit(unittest.TestCase):
    def test_emit(self):
        out = p._emit(["A", "B=1"], ["main.cpp", "x/y.c"])
        self.assertIn("set(GALAGINO_DEFINES\n    A\n    B=1\n)", out)
        self.assertIn("set(GALAGINO_SOURCES\n    main.cpp\n    x/y.c\n)", out)


if __name__ == "__main__":
    unittest.main()
