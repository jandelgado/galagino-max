"""ROM sets of the Senjyo / Star Force board, by game name.

File order per region follows the MAME ROM_LOAD offsets."""
from dataclasses import dataclass
from types import MappingProxyType


@dataclass(frozen=True)
class SetSpec:
    zip: str
    main: tuple[str, ...]
    sub: str
    fg: tuple[str, ...]
    bg1: tuple[str, ...]
    bg2: tuple[str, ...]
    bg3: tuple[str, ...]
    sprites: tuple[str, ...]


SETS = MappingProxyType({
    "starforce": SetSpec(
        zip="starforc.zip",
        main=("3.3p", "2.3mn"),
        sub="1.3hj",
        fg=("7.2fh", "8.3fh", "9.3fh"),
        bg1=("15.10jk", "14.9jk", "13.8jk"),
        bg2=("12.10de", "11.9de", "10.8de"),
        bg3=("18.10pq", "17.9pq", "16.8pq"),
        sprites=("6.10lm", "5.9lm", "4.8lm"),
    ),
    "baluba": SetSpec(
        zip="baluba.zip",
        main=("0", "1"),
        sub="2",
        fg=("15", "16", "17"),
        bg1=("9", "10", "11"),
        bg2=("12", "13", "14"),
        bg3=("8", "7", "6"),
        sprites=("5", "4", "3"),
    ),
})
