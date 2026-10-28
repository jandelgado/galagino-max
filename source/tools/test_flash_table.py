import unittest

from tools import flash_table

MACHINES_H = """
#ifdef GAME_PACMAN
  { createMachine<pacman>, pacman::logo,
    MCH_PACMAN, nullptr },
#endif
#ifdef GAME_1943
  { createMachine<_1943>, _1943::logo,
    MCH_1943, &_1943_arena_plan },
#endif
#if defined(GAME_TRON)
  { createMachine<tron>, tron::logo, MCH_TRON, nullptr },
#endif
"""


class ClassesByGame(unittest.TestCase):
    def test_classes_follow_the_game_guard(self):
        classes = flash_table.classes_by_game(MACHINES_H.splitlines())
        self.assertEqual(classes, {"PACMAN": {"pacman"}, "1943": {"_1943"}, "TRON": {"tron"}})


class Attribute(unittest.TestCase):
    CLASSES = {"PACMAN": {"pacman"}, "1943": {"_1943"}}

    def test_symbols_by_class_prefix_and_asset_prefix(self):
        symbols = [(100, "pacman::run_frame()"), (50, "pacman_rom_packed"), (7, "_1943::rdZ80(unsigned short)"),
                   (9, "_1943_rom_p03_packed"), (3, "(anonymous namespace)::pacman_tables"), (1000, "vfprintf")]
        per, unmatched = flash_table.attribute(symbols, self.CLASSES)
        self.assertEqual(per, {"PACMAN": 153, "1943": 16})
        self.assertEqual(unmatched, [(1000, "vfprintf")])

    def test_flash_address_ranges(self):
        self.assertTrue(flash_table.in_flash(0x400d1234))
        self.assertTrue(flash_table.in_flash(0x3f400100))
        self.assertFalse(flash_table.in_flash(0x3ffb0000))   # DRAM
        self.assertFalse(flash_table.in_flash(0x40080000))   # IRAM


if __name__ == "__main__":
    unittest.main()
