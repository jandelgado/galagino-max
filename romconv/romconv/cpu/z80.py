from romconv.core.context import Context
from romconv.core.job import Job
from romconv.cpu.pypatch import apply_patch

_ZIP = "Z80-081707.zip"

# Files copied verbatim from the vendor ZIP (CRLF normalized only).
# Z80.h / Z80.c additionally receive local modifications via the patch.
_FILES = ("CodesCB.h", "Codes.h", "CodesXX.h", "CodesED.h", "CodesXCB.h", "Tables.h", "Z80.h", "Z80.c")


def _convert(ctx: Context) -> None:
    dest = ctx.paths.src / "cpus" / "z80"
    dest.mkdir(parents=True, exist_ok=True)

    ctx.log.info(f"Load ZIP from: {ctx.zip_path(_ZIP)}")
    ctx.log.info(f"Target files:  {dest}")

    for name in _FILES:
        ctx.log.info(f"Copying {name}")
        (dest / name).write_bytes(ctx.rom(f"Z80/{name}").replace(b"\r\n", b"\n"))

    ctx.log.info("Patching Z80.h / Z80.c")
    apply_patch(ctx.paths.romconv / "internal" / "z80" / "z80.patch", dest)


JOB = Job(name="z80", convert=_convert, zips=(_ZIP,))
