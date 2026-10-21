# Taito SJ games without the 68705 MCU, MAME taito/taitosj.cpp `nomcu`:
#   elevator: Elevator Action, set elevatorb (bootleg, protection patched
#             out), files of romszip/elevator.zip
#   jungleh:  Jungle Hunt (US), clone of junglek, files of romszip/junglek.zip
# Main ROM: 0000-7fff, then bank 1 7000-7fff (ic52, MAME region 0x11000).
# Gfx ROM: read by the CPU through d404 and copied into character RAM, so
# tiles are decoded at render time, not here.
# Draw order: MAME taitosj_state::compute_draw_order() on the layer PROM,
# 32 priority values (d300 & 0x1f) x 4 planes back to front
# (0 = sprites, 1..3 = tilemaps).
from functools import partial

from romconv.core.context import Context
from romconv.core.job import Job
from romconv.emit.asset import emit_compressed


def _elevator_roms(ctx):
    def rom(names, sha1):
        return ctx.rom(names, sha1)

    main = (rom(["elevatorb/eabl_12.2732.ic69"], "fad660d2983daad478085be3b1a951b0864485dd") +
            rom(["elevatora/ea_13.2732.ic68"], "b72fea84f8322463ff224e3b06698a1ed7e305b7") +
            rom(["elevatora/ea_14.2732.ic67"], "11b68b89ab0f580bfe88047e59bd9bba237a2eb4") +
            rom(["elevatorb/eabl_15.2732.ic66"], "99f23d82d7866e4bca8f5a508e0913673d12489b") +
            rom(["elevatora/ea_16.2732.ic65"], "a7657d3d661421d1fca3b04e4025725272b77203") +
            rom(["elevatora/ea_17.2732.ic64"], "163daa07b6d45469f18e4f4a1904b60a890c8699") +
            rom(["elevatorb/eabl_18.2732.ic55"], "1de128cf96e6d976f6f09e4ac2b4d2507935bfe9") +
            rom(["elevatorb/eabl_19.2732.ic54"], "b2f684b61feb31e3d1856c16edcef33262f68581") +
            rom(["elevatorb/eabl.2732.ic52"], "e8980536f787fcb603943d4b2b3a64f475e51a16"))
    sound = (rom(["ba3__09.2732.ic70", "ea_9.2732.ic70"], "abb916d675ee85032697d656121d4f525202cab3") +
             rom(["ba3__10.2732.ic71", "ea_10.2732.ic71"], "9970fba3afeaaaa7fd217f0704fb9df9cf13cf65"))
    gfx = (rom(["elevatora/ea_20.2732.ic1"], "a8e3a0886ea5dc8e70aa280b4cef5fb26ca0e125") +
           rom(["elevatora/ea_21.2732.ic2"], "0ba292ac34dbf779a929db6358cd842d38077b3d") +
           rom(["elevatora/ea_22.2732.ic3"], "f1a18c09e31edb4ec3ad7ab853f425383ca22314") +
           rom(["elevatora/ea_23.2732.ic4"], "0767701213920d30d5a3a226b25cfbbd3f24437a") +
           rom(["elevatora/ea_24.2732.ic5"], "fe116c53a7e8ac523a17249a56df9f40b503b30d") +
           rom(["elevatora/ea_25.2732.ic6"], "646491c1d28904d9e662b1bff554bb74ec47708d") +
           rom(["elevatora/ea_26.2732.ic7"], "30bca7f5214bf424aa10184094947496f054ddf4") +
           rom(["elevatorb/eabl_27.2732.ic8"], "ec4db1392967f3959574bc4ca03e95938a6e5173"))
    prom = rom(["eb16.ic22"], "d233f1bf8a3e6cd876853ffd721b9b64c61c9047")
    return main, sound, gfx, prom


# MAME ROM_START( jungleh ): clone files under jungleh/, parent files at top
def _jungleh_roms(ctx):
    def kn(names_sha1):
        return b"".join(bytes(ctx.rom(["jungleh/" + n, n], h)) for n, h in names_sha1)

    main = kn([("kn41a", "d6de28766aab90b5dbca7f74612ec8eafd144348"),
               ("kn42.bin", "c3d2cf776598cb2d8684fa0b3ea7af90af9e8dae"),
               ("kn43.bin", "af961b671dc4c865d0181d08a70b902bb96f29d0"),
               ("kn44.bin", "57a1ddc07009f0dbd423cbe111b886e919a8bb0a"),
               ("kn45.bin", "7c387f0aeb9497b026d8838ee6ea7ff11dea506a"),
               ("kn46a", "bac17c9828002b644f15933149a205a008a561d3"),
               ("kn47.bin", "c57dec92998b971d76aecd23674c25cf7b8be667"),
               ("kn48a", "b3ddcc37860a2693d45a85970926662cbb96bd0e"),
               ("kn60.bin", "82f4cebeba90419e83a00427b671985824babd7a")])
    sound = kn([("kn57-1.bin", "84eadbc5c6a37c53c104e4ac1fd273b6b2a335e5"),
                ("kn58-1.bin", "867d9352cde4d6496f59e790cbbf15302a55364e"),
                ("kn59-1.bin", "1e0c52ec6b1d3cfd47247db71bcf3fe476c32039")])
    gfx = kn([("kn49a", "10c39abc49786154396c00bd35a51b826e5f6bd0"),
              ("kn50a", "b2d3ab8a53ef3ca55165a5bda9be0829f53be6c9"),
              ("kn51a", "de5f9b953f277986679ab958772571d8417a0ce2"),
              ("kn52a", "80569807ae36b4c5ad90e9e736ce9d0d0ea486ec"),
              ("kn53a", "d7ab6a240bb1ac80d3903cb694e55fd6d3670faa"),
              ("kn54a", "8c926db731073313daced31a168da6ac07a6d5cb"),
              ("kn55.bin", "df7454a1c3676181eca698bb3b2ef3253a45ca0f"),
              ("kn56a", "218cd75f77c858c3714a8f03aea2c7ee88a212dd")])
    prom = ctx.rom(["eb16.22"], "d233f1bf8a3e6cd876853ffd721b9b64c61c9047")
    return main, sound, gfx, prom


def _draw_order(prom):
    order = []
    for i in range(32):
        mask, planes = 0, [0] * 4
        for j in range(3, -1, -1):
            data = prom[0x10 * (i & 0x0f) + mask] & 0x0f
            data = data >> 2 if i & 0x10 else data & 0x03
            mask |= 1 << data
            planes[j] = data
        order += planes
    return order


_CHARRAM = 0x3000   # Arena::alloc in the machine ctor


def _convert(game: str, ctx: Context) -> None:
    main, sound, gfx, prom = {"elevator": _elevator_roms, "jungleh": _jungleh_roms}[game](ctx)

    order = _draw_order(prom)
    assert order[4 * 0x06:4 * 0x06 + 4] == [3, 2, 0, 1], order[24:28]   # attract: b m s f

    for asset, data in (("main_rom", main), ("sound_rom", sound), ("gfx_rom", gfx), ("draw_order", order)):
        name = f"{game}_{asset}"
        with ctx.header(f"{name}.h") as f:
            emit_compressed(f, name, "unsigned char", "", len(data), list(data))
        ctx.log.info(f"wrote {name}.h")

    sizes = {"main": len(main), "sound": len(sound), "gfx": len(gfx), "order": len(order), "charram": _CHARRAM}
    for k, v in sizes.items():
        ctx.log.info(f"  {k:8s} {v:6d}")
    total = sum(sizes.values())
    ctx.log.info(f"Arena total {total} B (budget 182000)")
    assert total < 182000


JOBS = (
    Job(name="elevator", convert=partial(_convert, "elevator"), machine="elevator", zips=("elevator.zip",)),
    Job(name="jungleh", convert=partial(_convert, "jungleh"), machine="jungleh", zips=("junglek.zip",)),
)
