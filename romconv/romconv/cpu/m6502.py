from pathlib import Path

from romconv.core.context import Context
from romconv.core.job import Job
from romconv.cpu.pypatch import apply_patch

_ZIP = "M6502-081707.zip"

_COPY = ("Codes.h", "M6502.c", "M6502.h", "Tables.h")
_RENAME = ("Codes6502.h", "M6502.c", "M6502.h", "Tables6502.h")


def _convert(ctx: Context) -> None:
    dest = ctx.paths.src / "cpus" / "mos6502"
    dest.mkdir(parents=True, exist_ok=True)

    ctx.log.info(f"Load ZIP from: {ctx.zip_path(_ZIP)}")
    ctx.log.info(f"Target files:  {dest}")

    for src, dst in zip(_COPY, _RENAME):
        ctx.log.info(f"Copying {src} \t->\t {dst}")
        (dest / dst).write_bytes(ctx.rom(f"M6502/{src}").replace(b"\r\n", b"\n"))

    ctx.log.info(f"Patching {', '.join(_RENAME)}")
    apply_patch(Path(__file__).parent / "patches" / "mos6502.patch", dest)


JOB = Job(name="m6502", convert=_convert, zips=(_ZIP,), always=True)
