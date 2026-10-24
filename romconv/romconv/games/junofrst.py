# Juno First (Konami 1983), MAME konami/junofrst.cpp, set junofrst (Konami
# parent; the Gottlieb clone junofrstg is the same hardware).
# Main KONAMI-1: fixed ROM a000-ffff, 12 banks of 4 KB at 9000-9fff (code
# and data, encrypted like the fixed ROM), blitter ROM read only by the
# blitter (nibble addressed). Sound: Z80 ROM + i8039 DAC program.
# Pens: RGB565 (byte swapped) of every palette RAM byte BBGGGRRR, MAME
# tutankhm_state::raw_to_rgb_func resistor weights.
from romconv.core.context import Context
from romconv.core.job import Job
from romconv.emit.asset import emit_compressed
from romconv.emit.write import write_asset
from romconv.gfx.color import rgb565_swapped
from romconv.gfx.resnet import combine, resistor_weights
from romconv.plan.budget import log_sizes

_RGB_MAXIMUM = 224   # MAME: headroom for stars and shells on the galaxian board


def _pens() -> list[int]:
    (rw, gw, bw), _ = resistor_weights(0, _RGB_MAXIMUM, -1.0, [([1000, 470, 220], 470, 0),
                                                               ([1000, 470, 220], 470, 0),
                                                               ([470, 220], 470, 0)])
    pens = []
    for c in range(256):
        r = combine(rw, c & 1, (c >> 1) & 1, (c >> 2) & 1)
        g = combine(gw, (c >> 3) & 1, (c >> 4) & 1, (c >> 5) & 1)
        b = combine(bw, (c >> 6) & 1, (c >> 7) & 1)
        pens.append(rgb565_swapped(r, g, b))
    return pens


def _convert(ctx: Context) -> None:
    def rom(name, sha1):
        return ctx.rom(name, sha1)

    fixed = (rom("jfa_b9.bin", "9603e797839290f8e1f93ccff9cc820604cc49ab")
             + rom("jfb_b10.bin", "46f58bdc1a613124e2c148b61f774fcc6c232868")
             + rom("jfc_a10.bin", "bee69833af886436016560295cddf0c8b4c5e771"))
    bank = (rom("jfc1_a4.bin", "02b45fe3c51bdc940919aac68136a121ed9bee18")
            + rom("jfc2_a5.bin", "a48e7de08647cbece7787c287217eac7e7a7510b")
            + rom("jfc3_a6.bin", "3c7af8767c9ce908fa1761180c6e585823216d8a")
            + rom("jfc4_a7.bin", "4d0e247e729365476dd3996c7d1f2a19fc83d773")
            + rom("jfc5_a8.bin", "c532aaed7f9e6f564e3df0dc6d8fdbee6ed721a2")
            + rom("jfc6_a9.bin", "de997d1b2ff6671088b57192bc9f1279359fad5d"))
    sound = rom("jfs1_j3.bin", "b90251c4971f7ba12e407f86c32723d513d6b4a0")
    i8039 = rom("jfs2_p4.bin", "9d0730d1d037bf96b0c933a32355602bf2d735dd")
    blit = (rom("jfs3_c7.bin", "f99ef9f9153d7a83e1881d9181faac99cb8c8a57")
            + rom("jfs4_d7.bin", "65494766676f18d8b5ae9a54cee00790e7b1e67e")
            + rom("jfs5_e7.bin", "f5471b9f6f1fa6d6e5d76300d89f71da3129516a"))

    assets = (("junofrst_rom", fixed), ("junofrst_bank", bank), ("junofrst_snd", sound),
              ("junofrst_i8039", i8039), ("junofrst_blit", blit))
    for name, data in assets:
        write_asset(ctx, name + ".h", None,
                    lambda f, name=name, data=data: emit_compressed(f, name, "unsigned char", "",
                                                                    len(data), list(data)))
    pens = _pens()
    write_asset(ctx, "junofrst_pens.h", None,
                lambda f: emit_compressed(f, "junofrst_pens", "unsigned short", "", len(pens), pens))

    sizes = {name: len(data) for name, data in assets}
    sizes["junofrst_pens"] = 2 * len(pens)
    log_sizes(ctx, sizes, 201500)


JOB = Job(name="junofrst", convert=_convert, machine="junofrst", zips=("junofrst.zip",))
