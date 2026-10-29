import tempfile
import unittest
from pathlib import Path

from tools import config_to_cmake as c

CFG = {
    "options": {"defines": ["BOOT_SELFTEST"]},
    "arena": {"rp2350": [200000, 0], "rp2350_remote": [100000, 0]},
    "games": {"builtin": ["pacman", "galaga"], "local": ["dkong"]},
    "modes": {"local": {"games": ["builtin", "local"]}, "remote": {"games": ["builtin"], "defines": ["ASSETS_REMOTE"]}},
}


class Defines(unittest.TestCase):
    def test_order(self):
        # HAL first, then options, mode, layout, extra
        self.assertEqual(c._defines(CFG, "local", "rp2350", "rp2350", None, ["DEBUG_TIMING"]),
                         ["HAL_RP2350", "BOOT_SELFTEST", "ENABLE_PACMAN", "ENABLE_GALAGA", "ENABLE_DKONG",
                          "ARENA_LAYOUT_RP2350", "ARENA_A_SIZE=200000", "ARENA_B_SIZE=0", "DEBUG_TIMING"])

    def test_only_replaces_games(self):
        got = c._defines(CFG, "local", "rp2350", "rp2350", ["dkong"], [])
        self.assertEqual([d for d in got if d.startswith("ENABLE_")], ["ENABLE_DKONG"])
        self.assertIn("BOOT_SELFTEST", got)

    def test_remote_mode(self):
        got = c._defines(CFG, "remote", "rp2350_remote", "rp2350", None, [])
        self.assertIn("ASSETS_REMOTE", got)

    def test_unknown_mode_or_layout(self):
        for mode, layout in (("nope", "rp2350"), ("local", "nope"), ("remote", "rp2350")):
            with self.subTest(mode=mode, layout=layout):
                with self.assertRaises(SystemExit) as cm:
                    c._defines(CFG, mode, layout, "rp2350", None, [])
                self.assertIn("nope" if "nope" in (mode, layout) else "rp2350", str(cm.exception))


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

    def test_selects_one_backend(self):
        got = c._sources(self.root, "rp2350", [])
        self.assertIn("hal/rp2350/time.cpp", got)
        self.assertIn("main.cpp", got)
        self.assertIn("cpus/m6502/m6502.c", got)
        self.assertNotIn("hal/esp/audio.cpp", got)
        self.assertNotIn("hal/host/x.cpp", got)
        self.assertNotIn("hal/hal.h", got)

    def test_net_sources_follow_assets_remote(self):
        remote = c._defines(CFG, "remote", "rp2350_remote", "rp2350", None, [])
        local = c._defines(CFG, "local", "rp2350", "rp2350", None, [])
        self.assertIn("net/loader.cpp", c._sources(self.root, "rp2350", remote))
        self.assertNotIn("net/loader.cpp", c._sources(self.root, "rp2350", local))


class Emit(unittest.TestCase):
    def test_emit(self):
        out = c._emit(["A", "B=1"], ["main.cpp", "x/y.c"])
        self.assertIn("set(GALAGINO_DEFINES\n    A\n    B=1\n)", out)
        self.assertIn("set(GALAGINO_SOURCES\n    main.cpp\n    x/y.c\n)", out)


if __name__ == "__main__":
    unittest.main()
