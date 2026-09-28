"""ROM sets of the Senjyo / Star Force board, selected by game name.

Scripts in this directory take the game as argv[1] (default starforce).
File order per region follows the MAME ROM_LOAD offsets.
"""

ROMS = "../../roms/"

SETS = {
    "starforce": {
        "main": ["3.3p", "2.3mn"],
        "sub": "1.3hj",
        "fg": ["7.2fh", "8.3fh", "9.3fh"],
        "bg1": ["15.10jk", "14.9jk", "13.8jk"],
        "bg2": ["12.10de", "11.9de", "10.8de"],
        "bg3": ["18.10pq", "17.9pq", "16.8pq"],
        "sprites": ["6.10lm", "5.9lm", "4.8lm"],
    },
    "baluba": {
        "main": ["0", "1"],
        "sub": "2",
        "fg": ["15", "16", "17"],
        "bg1": ["9", "10", "11"],
        "bg2": ["12", "13", "14"],
        "bg3": ["8", "7", "6"],
        "sprites": ["5", "4", "3"],
    },
}


def get(argv):
    """Return (game, set) for the game named in argv[1], default starforce."""
    game = argv[1] if len(argv) > 1 else "starforce"
    return game, SETS[game]


def rom(name):
    return ROMS + name


def out_header(game, suffix):
    return f"../../../source/src/machines/{game}/{game}_{suffix}.h"
