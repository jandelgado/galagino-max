"""Galaxian family: galaxian, mooncresta, amidar, scramble, supercobra, turtles.

Tile and sprite decode is in romconv.hw.galaxian."""
from collections.abc import Sequence
from functools import partial

from romconv.core.context import Context
from romconv.core.header import HeaderWriter
from romconv.core.job import Job
from romconv.emit.asset import emit_compressed
from romconv.gfx.gfxutil import hex16
from romconv.hw.galaxian import convert_sprites, convert_tiles


def _padded(data: bytearray, size: int) -> bytearray:
    if len(data) < size:
        data += bytearray([0xFF] * (size - len(data)))
    return data


# PROM bits 0-2 red, 3-5 green, 6-7 blue; weights of the resistor ladders
_WEIGHTS_GALAXIAN = ((0x21, 0x47, 0x97), (0x4F, 0xA8))
_WEIGHTS_AMIDAR = ((0x1D, 0x3E, 0x85), (0x48, 0x99))


def _colors(prom: Sequence[int], weights: tuple[tuple[int, ...], tuple[int, ...]]) -> list[int]:
    """32 PROM bytes -> byte-swapped RGB565 (ESP32 SPI display)."""
    rw, bw = weights
    rgb565 = []
    for i in range(32):
        bits = prom[i]
        r = rw[0] * ((bits >> 0) & 1) + rw[1] * ((bits >> 1) & 1) + rw[2] * ((bits >> 2) & 1)
        g = rw[0] * ((bits >> 3) & 1) + rw[1] * ((bits >> 4) & 1) + rw[2] * ((bits >> 5) & 1)
        b = bw[0] * ((bits >> 6) & 1) + bw[1] * ((bits >> 7) & 1)
        val = (((r >> 3) & 0x1F) << 11) | (((g >> 2) & 0x3F) << 5) | ((b >> 3) & 0x1F)
        rgb565.append(((val & 0xFF) << 8) | ((val >> 8) & 0xFF))
    return rgb565


def _write_rom(ctx: Context, header: str, symbol: str, title: str, data: Sequence[int]) -> None:
    with ctx.header(header) as f:
        f.write(f"// {title} program ROM ({len(data)} bytes)\n")
        emit_compressed(f, symbol, "unsigned char", "", len(data), list(data))
    ctx.log.info(f"Wrote: {header} ({len(data)} bytes)")


def _write_graphics(ctx: Context, name: str, title: str, plane0: Sequence[int],
                    plane1: Sequence[int]) -> None:
    tiles = convert_tiles(plane0, plane1)
    with ctx.header(f"{name}_tilemap.h") as f:
        f.write(f"// {title} tilemap: {len(tiles)} tiles, 8x8, 2bpp\n")
        flat = [v for rows in tiles for v in rows]
        emit_compressed(f, f"{name}_tilemap", "unsigned short", "[8]", len(tiles), flat)
    ctx.log.info(f"Wrote: {name}_tilemap.h ({len(tiles)} tiles)")

    sprites = convert_sprites(plane0, plane1)
    with ctx.header(f"{name}_spritemap.h") as f:
        f.write(f"// {title} spritemap: {len(sprites)} sprites, 16x16, 2bpp, unflipped\n")
        flat = [v for rows in sprites for v in rows]
        emit_compressed(f, f"{name}_spritemap", "uint32_t", "[16]", len(sprites), flat)
    ctx.log.info(f"Wrote: {name}_spritemap.h ({len(sprites)} sprites)")


def _write_colormap(f: HeaderWriter, name: str, title: str, rgb565: Sequence[int]) -> None:
    f.write(f"// {title} colormap: 8 palettes x 4 colors, RGB565\n")
    f.write(f"const unsigned short {name}_colormap[][4] = {{\n")
    for pal in range(8):
        colors = rgb565[pal * 4: pal * 4 + 4]
        f.write("  { " + ", ".join(hex16(c) for c in colors) + " }")
        if pal < 7:
            f.write(",")
        f.write(f"  // palette {pal}\n")
    f.write("};\n")


def _colormap(ctx: Context, name: str, title: str, rgb565: Sequence[int]) -> None:
    with ctx.header(f"{name}_cmap.h") as f:
        _write_colormap(f, name, title, rgb565)
    ctx.log.info(f"Wrote: {name}_cmap.h (8 palettes)")


def _convert_galaxian(ctx: Context) -> None:
    # ROM set galmidw: program 5 x 2K, graphics 1h.bin (plane 0) and 1k.bin (plane 1), PROM 6l.bpr
    program = (ctx.rom("galmidw.u") + ctx.rom("galmidw.v") + ctx.rom("galmidw.w") + ctx.rom("galmidw.y")
               + ctx.rom("7l"))
    _padded(program, 0x4000)
    _write_rom(ctx, "galaxian_rom.h", "galaxian_rom", "Galaxian", program)
    gfx_1h, gfx_1k = ctx.rom("1h.bin"), ctx.rom("1k.bin")
    _write_graphics(ctx, "galaxian", "Galaxian", gfx_1h, gfx_1k)
    _colormap(ctx, "galaxian", "Galaxian", _colors(ctx.rom("6l.bpr"), _WEIGHTS_GALAXIAN))


# Moon Cresta (Nichibutsu): the program ROMs are encrypted. mc5, mc6, mc7 are the
# files the old unpacked romconv/roms/ called mc5.7r, mc6.8d, mc7.8e (same SHA-1).
def _bitswap(value: int, *positions: int) -> int:
    """Reorders the bits of an 8-bit byte based on target positions."""
    res = 0
    for i, pos in enumerate(reversed(positions)):
        if (value >> pos) & 1:
            res |= (1 << i)
    return res


def _decrypt_moon_cresta(input_data: Sequence[int]) -> bytearray:
    """The Nichibutsu hardware decryption."""
    output_data = bytearray(len(input_data))
    for offs in range(len(input_data)):
        data = input_data[offs]
        res = data
        if data & (1 << 1):  # bit 1 set: XOR bit 6
            res ^= 0x40
        if data & (1 << 5):  # bit 5 set: XOR bit 2
            res ^= 0x04
        if not (offs & 1):  # even addresses: swap bit 2 and bit 6
            res = _bitswap(res, 7, 2, 5, 4, 3, 6, 1, 0)
        output_data[offs] = res
    return output_data


def _mooncresta_colors(prom: Sequence[int]) -> list[int]:
    rgb565 = _colors(prom, _WEIGHTS_GALAXIAN)
    # patch palette 5: fixes ship III colors and credits line (swapped RGB565)
    for i, val in ((20, 0b0000000000000000),   # transparent
                   (21, 0b1101011010000000),   # yellow  - III
                   (22, 0b0000011100000000),   # green   - outline
                   (23, 0b0110101101011110)):  # magenta - body
        rgb565[i] = ((val & 0xFF) << 8) | ((val >> 8) & 0xFF)
    return rgb565


def _convert_mooncresta(ctx: Context) -> None:
    encrypted = bytearray()
    for name in ("mc1", "mc2", "mc3", "mc4", "mc5", "mc6", "mc7", "mc8"):
        encrypted += ctx.rom(name)
    program = _padded(_decrypt_moon_cresta(encrypted), 0x4000)
    _write_rom(ctx, "mooncresta_rom.h", "mooncresta_rom", "Moon Cresta", program)
    plane0 = ctx.rom("mcs_b") + ctx.rom("mcs_d")
    plane1 = ctx.rom("mcs_a") + ctx.rom("mcs_c")
    _write_graphics(ctx, "mooncresta", "Moon Cresta", plane0, plane1)
    _colormap(ctx, "mooncresta", "Moon Cresta", _mooncresta_colors(ctx.rom("l06_prom.bin")))


def _convert_amidar(ctx: Context) -> None:
    # ROM set amidar (MAME); main CPU 5 x 4K, audio CPU 2 x 4K, gfx 5f/5d, PROM amidar.clr
    main_cpu = (ctx.rom("1.2c", "f064eccfb7da18119ed3088a5f939085eb446c90")
                + ctx.rom("2.2e", "12b2a0c09926d006781bee5d450bc0c391cc1fb5")
                + ctx.rom("3.2f", "e83f049b25aba481e09606db3158726145ebd656")
                + ctx.rom("4.2h", "9ef1d27f0780612be0ea2be94c3a2c781a4924c8")
                + ctx.rom("5.2j", "1530b374d15e0d05c8eb988cc1cbab48b0be211c"))
    audio_cpu = (ctx.rom("s1.5c", "4f4c2915503b85abe141d717fd254ee10c9da99e")
                 + ctx.rom("s2.5d", "84d953618c8bf510d23b42232a856ac55f1baff5"))
    rom_5f = ctx.rom("c2.5f", "0d86a78008ac8653c17fff5be5ebdf1f0a9d31eb")
    rom_5h = ctx.rom("c2.5d", "8764deec9fbff4220d61df621b12fc36c3702601")
    prom = ctx.rom("amidar.clr", "1015e56f37c244a850a8f4bf0e36668f047fd46d")

    _write_rom(ctx, "amidar_main_rom.h", "amidar_main_rom", "Amidar", main_cpu)
    _write_rom(ctx, "amidar_audio_rom.h", "amidar_audio_rom", "Amidar", audio_cpu)
    _write_graphics(ctx, "amidar", "Amidar", rom_5f, rom_5h)  # 5f = plane 0, 5h = plane 1
    _colormap(ctx, "amidar", "Amidar", _colors(prom, _WEIGHTS_AMIDAR))


def _convert_scramble(ctx: Context) -> None:
    # names of current MAME first, then the older dump names
    main_cpu = bytearray()
    for names, sha1 in ((["2d.k", "s1.2d"], "1dcb375987fe21e0483c27d485c405de53848d61"),
                        (["2e.k", "s2.2e"], "240877576045fddcc9ff01d97dc78139454ac4f1"),
                        (["2f.k", "s3.2f"], "a84d191c7be8700f630a83ddad798be9e83b5d55"),
                        (["2h.k", "s4.2h"], "5d155808c19dcf2e14aa8e29c0ee41a6d3d3c43a"),
                        (["2j.k", "s5.2j"], "9cb5861c61e4783e5fbaa3869d51195f127b1129"),
                        (["2l.k", "s6.2l"], "67c0fa81729370631647b5d78bb5a61433facd7f"),
                        (["2m.k", "s7.2m"], "05a6fe3010c2136284ca76352dac147797c79778"),
                        (["2p.k", "s8.2p"], "e3b09141cee26857d626412e9d1a0e759469b97a")):
        main_cpu += ctx.rom(names, sha1)
    audio_cpu = bytearray()
    for names, sha1 in ((["5c", "ot1.5c"], "8ed78487d76fd0a917ab7b258937a46e2cd9800c"),
                        (["5d", "ot2.5d"], "8558b4eff5d7e63029b325edef9914feda5834c3"),
                        (["5e", "ot3.5e"], "1f976d8595706730e29f93027e7ab4620075c078")):
        audio_cpu += ctx.rom(names, sha1)
    rom_5f = ctx.rom(["5f.k", "c2.5f"], "a8b1ad19a95a9d35050a2ab7194cc96fc5afcdc9")
    rom_5h = ctx.rom(["5h.k", "c1.5h"], "69844e48bb4d372cac7ae83c953df573c7ecbb7f")
    prom = ctx.rom(["82s123.6e", "c01s.6e"], "a25083c3e36d28afdefe4af6e6d4f3155e303625")

    _write_rom(ctx, "scramble_main_rom.h", "scramble_main_rom", "Scramble", _padded(main_cpu, 0x4000))
    _write_rom(ctx, "scramble_audio_rom.h", "scramble_audio_rom", "Scramble", _padded(audio_cpu, 0x2000))
    _write_graphics(ctx, "scramble", "Scramble", rom_5f, rom_5h)
    _colormap(ctx, "scramble", "scramble", _colors(prom, _WEIGHTS_GALAXIAN))


def _convert_supercobra(ctx: Context) -> None:
    main_cpu = bytearray()
    for name, sha1 in (("epr1265.2c", "8949298a04f8ba8a82d5d84a7b012a0e7cff11df"),
                       ("2e", "281504ff364c3ddbf901c92729b139afd93b9785"),
                       ("epr1267.2f", "01775ad11dc23469649539ee8fb8a5800df031c6"),
                       ("2h", "f5fff565ed3f6c5f277a4db53c9f569813fcec1d"),
                       ("epr1269.2j", "2add8270352d6596052d3ff22c891ceccaa92071"),
                       ("2l", "5681771ed51d504bdcc2999fcbf926a30b137828")):
        main_cpu += ctx.rom(name, sha1)
    audio_cpu = bytearray()
    for name, sha1 in (("5c", "5eab4505beb69a5bdd88b23db60e1193371250cf"),
                       ("5d", "2b0784c4d05c466e0b7648f16e14f34393d792c3"),
                       ("5e", "ec79a73e4a2d7373454b227dd7eff255f1cc60cc")):
        audio_cpu += ctx.rom(name, sha1)
    rom_5h = ctx.rom("epr1274.5h", "7b439bb74d5ecc792e0ca8964bcca8c6b7a51262")
    rom_5f = ctx.rom("epr1273.5f", "9de0e94932e91dc34aea7c81880bde6a486d103b")
    prom = ctx.rom("82s123.6e", "d11ac5e4a6057301ea2a9cbb404c2b978eb4c1dc")

    _write_rom(ctx, "supercobra_main_rom.h", "supercobra_main_rom", "Super Cobra",
               _padded(main_cpu, 0x4000))  # 0x3000 of ROM
    _write_rom(ctx, "supercobra_audio_rom.h", "supercobra_audio_rom", "Super Cobra",
               _padded(audio_cpu, 0x2000))
    _write_graphics(ctx, "supercobra", "Super Cobra", rom_5h, rom_5f)
    _colormap(ctx, "supercobra", "scobra", _colors(prom, _WEIGHTS_GALAXIAN))


def _convert_turtles(ctx: Context) -> None:
    main_cpu = bytearray()
    for name, sha1 in (("turt_vid.2c", "3ca89800fda7a7e61f54d71d5302908be2706def"),
                       ("turt_vid.2e", "af74602bf2454eb8f3b9bb5c425e2476feeecd69"),
                       ("turt_vid.2f", "2af9383e5a289c2d7fbe6cf5e5b1519c352afbab"),
                       ("turt_vid.2h", "3dcdf5dc601c875fc9d8b9a46e3ef588e7478e0d"),
                       ("turt_vid.2j", "bb1e91b2e6d4b5a861bf37907ef6b198328d8d83")):
        main_cpu += ctx.rom(name, sha1)
    audio_cpu = (ctx.rom("turt_snd.5c", "5621f336e9be8acf986a34bbb8855ed5d45c28ef")
                 + ctx.rom("turt_snd.5d", "8a49c55feba094b07380615cf0b6f0878c25a260"))
    rom_5h = ctx.rom("turt_vid.5h", "bc3f52cf6c6e19dfd2dacd1e8c9128f437e995fc")
    rom_5f = ctx.rom("turt_vid.5f", "dee51d77be262a2944488e381541c10a2b6e5d83")
    prom = ctx.rom("turtles.clr", "09fd795170d7d30f101d579f57553da5ff3800ab")

    _write_rom(ctx, "turtles_main_rom.h", "turtles_main_rom", "Turtles", main_cpu)
    _write_rom(ctx, "turtles_audio_rom.h", "turtles_audio_rom", "Turtles", audio_cpu)
    _write_graphics(ctx, "turtles", "Turtles", rom_5h, rom_5f)
    _colormap(ctx, "turtles", "Turtles", _colors(prom, _WEIGHTS_GALAXIAN))


def _job(name: str, convert, zip: str, deps: tuple[str, ...] = ()) -> Job:
    return Job(name=name, convert=convert, machine=name, zips=(zip,), deps=deps)


# deps: the class hierarchy constructs the base games' assets (remote bundle)
JOBS = (
    _job("galaxian", _convert_galaxian, "galaxian.zip"),
    _job("mooncresta", _convert_mooncresta, "mooncrst.zip"),
    _job("scramble", _convert_scramble, "scramble.zip"),
    _job("supercobra", _convert_supercobra, "scobra.zip", deps=("scramble",)),
    _job("turtles", _convert_turtles, "turtles.zip", deps=("scramble",)),
    _job("amidar", _convert_amidar, "amidar.zip", deps=("turtles", "scramble")),
)
