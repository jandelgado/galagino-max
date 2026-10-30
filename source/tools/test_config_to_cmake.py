import tempfile
import unittest
from pathlib import Path

from tools import config_to_cmake as c

from tools import firmware_config as fc

CFG = {
    "options": {"defines": ["BOOT_SELFTEST"]},
    "arena": {"rp2350": [200000, 0], "rp2350_remote": [100000, 0]},
    "games": {"builtin": ["pacman", "galaga"], "local": ["dkong"]},
    "modes": {"local": {"games": ["builtin", "local"]}, "remote": {"games": ["builtin"], "defines": ["ASSETS_REMOTE"]}},
    "envs": {"p-local": {"platform": "rp2350", "board": "pico2", "mode": "local", "layout": "rp2350"},
             "p-remote": {"platform": "rp2350", "board": "pico2", "mode": "remote", "layout": "rp2350_remote"}},
}


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
        remote = fc.env_defines(CFG, "p-remote")
        local = fc.env_defines(CFG, "p-local")
        self.assertIn("net/loader.cpp", c._sources(self.root, "rp2350", remote))
        self.assertNotIn("net/loader.cpp", c._sources(self.root, "rp2350", local))


class Emit(unittest.TestCase):
    def test_emit(self):
        out = c._emit("pico2", ["A", "B=1"], ["main.cpp", "x/y.c"])
        self.assertIn("set(GALAGINO_BOARD pico2)\n", out)
        self.assertIn("set(GALAGINO_DEFINES\n    A\n    B=1\n)", out)
        self.assertIn("set(GALAGINO_SOURCES\n    main.cpp\n    x/y.c\n)", out)


if __name__ == "__main__":
    unittest.main()
