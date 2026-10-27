"""Build combination checks (src/hal/config_check.h, src/boards/): each rule
rejects a bad combination, every board profile passes.

  python3 -m unittest tools.test_config_check     (from source/)

Runs on a copy of the config headers: the developer's config_local.h stays
out.
"""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

_REAL_SRC = Path(__file__).resolve().parent.parent / "src"
_tmp = tempfile.TemporaryDirectory()
_SRC = Path(_tmp.name)
_CHECK = _SRC / "hal" / "config_check.h"
_CONFIG = _SRC / "config.h"
# every build gets these from extraScript.py (custom_arena_layout)
_ARENA = ["ARENA_A_SIZE=1", "ARENA_B_SIZE=1"]


def setUpModule():
    shutil.copy(_REAL_SRC / "config.h", _CONFIG)
    shutil.copytree(_REAL_SRC / "boards", _SRC / "boards")
    (_SRC / "hal").mkdir()
    shutil.copy(_REAL_SRC / "hal" / "config_check.h", _CHECK)


def tearDownModule():
    _tmp.cleanup()


def _macros(defines):
    args = ["g++", "-dM", "-E", "-x", "c++"] + ["-D" + d for d in defines] + [str(_CONFIG)]
    return subprocess.run(args, capture_output=True, text=True)


def _preprocess(header, defines):
    args = ["g++", "-E", "-x", "c++", "-o", "/dev/null"] + ["-D" + d for d in defines] + [str(header)]
    return subprocess.run(args, capture_output=True, text=True)


class Rules(unittest.TestCase):
    def _rejects(self, defines, message):
        r = _preprocess(_CHECK, defines + _ARENA)
        self.assertNotEqual(r.returncode, 0, defines)
        self.assertIn(message, r.stderr)

    def test_arena_layout_needed(self):
        r = _preprocess(_CHECK, ["HAL_ESP"])
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("set custom_arena_layout", r.stderr)

    def test_one_platform(self):
        self._rejects(["HAL_ESP", "HAL_RP2350"], "only one of HAL_ESP")

    def test_one_board(self):
        self._rejects(["HAL_ESP", "BOARD_FNK0103F", "BOARD_FNK0103B"], "only one BOARD_")

    def test_pico_needs_rp2350(self):
        self._rejects(["HAL_ESP", "BOARD_PICO2"], "BOARD_PICO2 needs HAL_RP2350")

    def test_esp_board_needs_esp(self):
        self._rejects(["HAL_RP2350", "BOARD_FNK0104"], "ESP32 board profiles need HAL_ESP")

    def test_one_video(self):
        self._rejects(["HAL_ESP", "HAL_VIDEO_NULL", "HAL_VIDEO_SPI_TFT", "TFT_CS=1", "TFT_DC=2"],
                      "only one HAL_VIDEO_")

    def test_dvi_needs_rp2350(self):
        self._rejects(["HAL_ESP", "HAL_VIDEO_DVI_HSTX"], "HAL_VIDEO_DVI_HSTX needs HAL_RP2350")

    def test_spi_tft_needs_pins(self):
        self._rejects(["HAL_ESP", "HAL_VIDEO_SPI_TFT", "TFT_CS=5"], "needs TFT_CS and TFT_DC")

    def test_one_audio(self):
        self._rejects(["HAL_ESP", "HAL_AUDIO_ESP_DAC", "HAL_AUDIO_ES8311"], "only one HAL_AUDIO_")

    def test_esp_dac_needs_esp(self):
        self._rejects(["HAL_RP2350", "HAL_AUDIO_ESP_DAC"], "HAL_AUDIO_ESP_DAC needs HAL_ESP")

    def test_es8311_needs_esp(self):
        self._rejects(["HAL_RP2350", "HAL_AUDIO_ES8311"], "HAL_AUDIO_ES8311 needs HAL_ESP")

    def test_es8311_needs_pins(self):
        self._rejects(["HAL_ESP", "HAL_AUDIO_ES8311", "ES8311_I2C_SDA=1"], "HAL_AUDIO_ES8311 needs ES8311_")

    def test_dvi_video_needs_dvi_audio(self):
        self._rejects(["HAL_RP2350", "HAL_VIDEO_DVI_HSTX", "HAL_AUDIO_NULL"],
                      "HAL_VIDEO_DVI_HSTX needs HAL_AUDIO_DVI")

    def test_one_i2c_input(self):
        self._rejects(["HAL_ESP", "NUNCHUCK_INPUT", "GALAGINO_CONTROLLER", "NUNCHUCK_SDA=1",
                       "NUNCHUCK_SCL=2", "NUNCHUCK_MOVE_THRESHOLD=30"],
                      "only one of NUNCHUCK_INPUT, GALAGINO_CONTROLLER")

    def test_nunchuk_needs_pins(self):
        self._rejects(["HAL_ESP", "NUNCHUCK_INPUT", "NUNCHUCK_SDA=1"], "NUNCHUCK_INPUT needs NUNCHUCK_")

    def test_dvi_audio_needs_dvi_video(self):
        self._rejects(["HAL_RP2350", "HAL_VIDEO_NULL", "HAL_AUDIO_DVI"], "HAL_AUDIO_DVI needs HAL_VIDEO_DVI_HSTX")


class Profiles(unittest.TestCase):
    def test_every_profile_passes(self):
        boards = {"BOARD_CYD2432S028R": "HAL_ESP", "BOARD_FNK0103F": "HAL_ESP",
                  "BOARD_FNK0103B": "HAL_ESP", "BOARD_FNK0104": "HAL_ESP",
                  "BOARD_PICO2": "HAL_RP2350"}
        for board, platform in boards.items():
            with self.subTest(board=board):
                r = _preprocess(_CONFIG, [platform, board] + _ARENA)
                self.assertEqual(r.returncode, 0, r.stderr)

    def test_every_profile_file_parses(self):
        # also the profiles no env selects (cyd_manual, galagino_default)
        for header in sorted((_SRC / "boards").glob("*.h")):
            with self.subTest(header=header.name):
                r = _preprocess(header, ["HAL_ESP"])
                self.assertEqual(r.returncode, 0, r.stderr)

    def test_old_spi_host_name_is_rejected(self):
        r = _preprocess(_CONFIG, ["HAL_ESP", "BOARD_FNK0103F", "TFT_SPI_HOST=SPI3_HOST"] + _ARENA)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("TFT_SPI_HOST is now TFT_SPI_PORT", r.stderr)

    def test_build_flag_overrides_profile(self):
        # a pin and a backend of the profile, replaced by build flags
        r = _macros(["HAL_ESP", "BOARD_CYD2432S028R", "TFT_CS=7", "HAL_AUDIO_NULL"] + _ARENA)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("#define TFT_CS 7\n", r.stdout)
        self.assertIn("#define HAL_AUDIO_NULL", r.stdout)
        self.assertNotIn("HAL_AUDIO_ESP_DAC", r.stdout)

    def test_default_backend_is_checked(self):
        # no profile (pins from config_local.h): the platform default
        # HAL_VIDEO_SPI_TFT still needs its pins
        r = _preprocess(_CONFIG, ["HAL_ESP", "USE_PIO_CONFIG"] + _ARENA)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("needs TFT_CS and TFT_DC", r.stderr)

    def test_cyd_nunchuk_replaces_the_controller(self):
        r = _macros(["HAL_ESP", "BOARD_CYD2432S028R", "NUNCHUCK_INPUT"] + _ARENA)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertNotIn("GALAGINO_CONTROLLER ", r.stdout)
        self.assertNotIn("#define GALAGINO_CONTROLLER\n", r.stdout)
        self.assertIn("#define NUNCHUCK_SDA 22\n", r.stdout)
        self.assertIn("#define NUNCHUCK_SCL 27\n", r.stdout)

    def test_fnk0104_nunchuk_shares_the_codec_bus(self):
        r = _macros(["HAL_ESP", "BOARD_FNK0104", "NUNCHUCK_INPUT"] + _ARENA)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertNotIn("#define GALAGINO_CONTROLLER\n", r.stdout)
        self.assertIn("#define NUNCHUCK_SDA 16\n", r.stdout)
        self.assertIn("#define NUNCHUCK_SCL 15\n", r.stdout)

    def test_usbup_flips_the_panel(self):
        r = _macros(["HAL_ESP", "BOARD_FNK0103B", "BOARD_USBUP"] + _ARENA)
        self.assertNotIn("#define TFT_VFLIP", r.stdout)


if __name__ == "__main__":
    unittest.main()
