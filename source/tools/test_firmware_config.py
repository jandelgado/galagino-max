import configparser
import tempfile
import unittest
from pathlib import Path

from tools import firmware_config as fc

CFG = {
    "options": {"defines": ["OPT", "VAL=3"]},
    "arena": {"esp32_local": [118784, 110500], "x_remote": [60000, 110500], "rp2350": [200000, 0]},
    "games": {"a": ["x", "y"], "b": ["y", "z"]},
    "modes": {"m": {"games": ["a", "b"], "defines": ["D"]}, "r": {"games": ["a"], "defines": ["ASSETS_REMOTE"]}},
    "envs": {
        "dev-m": {"platform": "esp", "board": "fnk0104", "mode": "m", "layout": "esp32_local"},
        "dev-r": {"platform": "rp2350", "board": "pico2", "mode": "r", "layout": "x_remote"},
        "native": {"platform": "host", "layout": "esp32_local"},
    },
}
_LAYOUT = ["ARENA_LAYOUT_ESP32_LOCAL", "ARENA_A_SIZE=118784", "ARENA_B_SIZE=110500"]


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

    def test_every_env_resolves(self):
        cfg = fc.load()
        for name in cfg["envs"]:
            with self.subTest(env=name):
                fc.env_defines(cfg, name)

    def test_platformio_envs_match(self):
        # PlatformIO builds the esp and host rows, each one of its [env:] sections
        ini = configparser.ConfigParser(interpolation=None)
        ini.read(fc.CONFIG_PATH.parent / "platformio.ini")
        pio = {s[len("env:"):] for s in ini.sections() if s.startswith("env:")}
        envs = fc.load()["envs"]
        self.assertEqual(pio, {n for n, row in envs.items() if row["platform"] in ("esp", "host")})

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

    def test_mode_excludes_games(self):
        cfg = dict(CFG, modes={"m": {"games": ["a", "b"], "exclude": ["y"]}})
        self.assertEqual(fc.mode_defines(cfg, "m"), ["ENABLE_X", "ENABLE_Z"])

    def test_unknown_excluded_game(self):
        cfg = dict(CFG, modes={"m": {"games": ["a"], "exclude": ["z"]}})
        with self.assertRaisesRegex(fc.ConfigError, "z"):
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


class EnvDefines(unittest.TestCase):
    def test_device_env(self):
        self.assertEqual(fc.env_defines(CFG, "dev-m"),
                         ["HAL_ESP", "BOARD_FNK0104", "OPT", "VAL=3", "D", "ENABLE_X", "ENABLE_Y",
                          "ENABLE_Z"] + _LAYOUT)

    def test_remote_env(self):
        self.assertEqual(fc.env_defines(CFG, "dev-r"),
                         ["HAL_RP2350", "BOARD_PICO2", "OPT", "VAL=3", "ASSETS_REMOTE", "ENABLE_X",
                          "ENABLE_Y", "ARENA_LAYOUT_X_REMOTE", "ARENA_A_SIZE=60000", "ARENA_B_SIZE=110500"])

    def test_host_env(self):
        # no board, no mode: no options, no games
        self.assertEqual(fc.env_defines(CFG, "native"), ["HAL_HOST"] + _LAYOUT)

    def test_games_replace_the_mode_games(self):
        got = fc.env_defines(CFG, "dev-m", games=["z"], extra=["DEBUG_TIMING"])
        self.assertEqual([d for d in got if d.startswith("ENABLE_")], ["ENABLE_Z"])
        self.assertIn("D", got)
        self.assertEqual(got[-1], "DEBUG_TIMING")

    def test_unknown_game(self):
        with self.assertRaisesRegex(fc.ConfigError, "nope"):
            fc.env_defines(CFG, "dev-m", games=["nope"])

    def test_games_need_a_mode(self):
        with self.assertRaisesRegex(fc.ConfigError, "native"):
            fc.env_defines(CFG, "native", games=["x"])

    def test_extra_joins_layout_check(self):
        with self.assertRaisesRegex(fc.ConfigError, "esp32_local"):
            fc.env_defines(CFG, "dev-m", extra=["ASSETS_REMOTE"])

    def test_bad_rows(self):
        rows = {"unknown": None,
                "platform": {"platform": "avr", "layout": "esp32_local"},
                "layout": {"platform": "host"},
                "device": {"platform": "esp", "mode": "m", "layout": "esp32_local"},
                "keys": {"platform": "host", "layout": "esp32_local", "games": ["x"]},
                "pairing": {"platform": "esp", "board": "b", "mode": "m", "layout": "x_remote"}}
        for name, row in rows.items():
            with self.subTest(name=name):
                envs = {} if row is None else {name: row}
                with self.assertRaisesRegex(fc.ConfigError, name if row is None else "x_remote|" + name):
                    fc.env_defines(dict(CFG, envs=envs), name)

    def test_dev_overrides(self):
        self.assertEqual(fc.dev_overrides({}), (None, []))
        self.assertEqual(fc.dev_overrides({"GAMES": "a,b", "DEFINES": "X=1 Y"}), (["a", "b"], ["X=1", "Y"]))


class Load(unittest.TestCase):
    def test_bad_toml(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "bad.toml"
            path.write_text("[arena\n")
            with self.assertRaisesRegex(fc.ConfigError, "bad.toml"):
                fc.load(path)


if __name__ == "__main__":
    unittest.main()
