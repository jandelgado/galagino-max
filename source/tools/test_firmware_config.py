import tempfile
import unittest
from pathlib import Path

from tools import firmware_config as fc

CFG = {
    "options": {"defines": ["OPT", "VAL=3"]},
    "arena": {"esp32_local": [118784, 110500], "x_remote": [60000, 110500], "rp2350": [200000, 0]},
    "games": {"a": ["x", "y"], "b": ["y", "z"]},
    "modes": {"m": {"games": ["a", "b"], "defines": ["D"]}, "r": {"games": ["a"], "defines": ["ASSETS_REMOTE"]}},
}


class RepoConfig(unittest.TestCase):
    def test_repo_config_loads(self):
        cfg = fc.load()
        self.assertIn("esp32_local", cfg["arena"])
        self.assertIn("esp32_remote", cfg["arena"])
        self.assertLessEqual({"local", "max", "full", "remote"}, set(cfg["modes"]))
        for mode in cfg["modes"]:
            fc.mode_defines(cfg, mode)
        for layout in cfg["arena"]:
            fc.layout_defines(cfg, layout)

    def test_games_disjoint(self):
        games = fc.load()["games"]
        seen = {}
        for name in ("builtin", "local", "rest"):
            for game in games[name]:
                self.assertNotIn(game, seen, "%s in %s and %s" % (game, seen.get(game), name))
                seen[game] = name


class ModeDefines(unittest.TestCase):
    def test_mode_defines_order(self):
        self.assertEqual(fc.mode_defines(CFG, "m"), ["D", "ENABLE_X", "ENABLE_Y", "ENABLE_Z"])

    def test_unknown_mode(self):
        with self.assertRaisesRegex(fc.ConfigError, "nope"):
            fc.mode_defines(CFG, "nope")

    def test_unknown_game_list(self):
        cfg = dict(CFG, modes={"m": {"games": ["missing"]}})
        with self.assertRaisesRegex(fc.ConfigError, "missing"):
            fc.mode_defines(cfg, "m")

    def test_option_defines(self):
        self.assertEqual(fc.option_defines(CFG), ["OPT", "VAL=3"])


class LayoutDefines(unittest.TestCase):
    def test_layout_defines(self):
        self.assertEqual(fc.layout_defines(CFG, "rp2350"),
                         ["ARENA_LAYOUT_RP2350", "ARENA_A_SIZE=200000", "ARENA_B_SIZE=0"])

    def test_unknown_layout(self):
        with self.assertRaisesRegex(fc.ConfigError, "nope"):
            fc.layout_defines(CFG, "nope")

    def test_bad_layout_row(self):
        for name, row in (("x", [1, 2, 3]), ("x", [0, 2]), ("x", [-1, 2]), ("x", ["a", "b"]),
                          ("x", [True, 2]), ("x", 5), ("bad-name", [1, 2])):
            with self.subTest(name=name, row=row):
                with self.assertRaisesRegex(fc.ConfigError, name):
                    fc.layout_defines({"arena": {name: row}}, name)

    def test_layout_mode_mismatch(self):
        with self.assertRaisesRegex(fc.ConfigError, "esp32_local"):
            fc.check_layout_mode("esp32_local", ["ASSETS_REMOTE"])
        with self.assertRaisesRegex(fc.ConfigError, "esp32_remote"):
            fc.check_layout_mode("esp32_remote", [])
        fc.check_layout_mode("esp32_remote", ["ASSETS_REMOTE"])
        fc.check_layout_mode("esp32_local", [])

    def test_build_defines(self):
        self.assertEqual(fc.build_defines(CFG, "r", "x_remote"),
                         ["OPT", "VAL=3", "ASSETS_REMOTE", "ENABLE_X", "ENABLE_Y",
                          "ARENA_LAYOUT_X_REMOTE", "ARENA_A_SIZE=60000", "ARENA_B_SIZE=110500"])
        with self.assertRaises(fc.ConfigError):
            fc.build_defines(CFG, "m", "x_remote")


class Load(unittest.TestCase):
    def test_bad_toml(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "bad.toml"
            path.write_text("[arena\n")
            with self.assertRaisesRegex(fc.ConfigError, "bad.toml"):
                fc.load(path)


if __name__ == "__main__":
    unittest.main()
