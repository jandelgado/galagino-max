"""Convert the MAME mrgoemon ROM set into galagino headers (MAME konami/gberet.cpp)."""
from types import MappingProxyType

from romconv.core.context import Context
from romconv.core.job import Job
from romconv.games.gberet import convert_set

# MAME ROM_START( mrgoemon )
_FILES = MappingProxyType(dict(
    main=(
        ("621d01.10c", "274160be5dabbbfa61af71d92bddffbb56eadab6"),
        ("621d02.12c", "6fd5f365b2624a37f252c202cd97877705b4a6c2"),
    ),
    tiles=("621a05.6d", "395024ebfff550b0da393096483196fb1152a077"),
    sprites=(
        ("621d03.4d", "7e906f258a5f4928f9615c6ea176efbca659b3a7"),
        ("621d04.5d", "e675c070e46993d3453c2ddadc49ec8b84cec854"),
    ),
    pal=("621a06.5f", "8ac5708e72e32f3d79ccde0cbaedefc34f8ac57e"),
    tile_clut=("621a08.7f", "ceb909ad96c0dabc8684e69b028f4287e227c351"),
    spr_clut=("621a07.6f", "f4e0bd74bccd77b84096c38bc70cf488a42d9562"),
))


def _convert(ctx: Context) -> None:
    # arena split comes from the plan, not the gberet block check
    convert_set(ctx, "mrgoemon", "Mr. Goemon", _FILES, "Z80 ROM 0x0000-0xffff, 0xc000+ banked",
                ("621a08", "621a07"), check_arena=False)


JOB = Job(name="mrgoemon", convert=_convert, machine="mrgoemon", zips=("mrgoemon.zip",), plan=True, deps=("gberet",))
