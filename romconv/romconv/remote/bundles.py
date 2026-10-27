# Remote bundles of the catalog games (mode 3, ASSETS_REMOTE): reads the
# sidecars the game jobs persist in romconv/build/assets/<dir>/, writes one
# wire bundle per _remote Arena layout to galagino-server/data/g/<hash> and
# <game>_bundle.h with the BundleRef the build's layout selects. Reports the
# ratio cost of the 64 KB dictionary of PLAIN assets.
#
# Builds the catalog games that were requested; a full run requests them all.
import json
import re
from pathlib import Path

from romconv.core.context import Context
from romconv.core.errors import ConversionError
from romconv.core.job import Job
from romconv.core.paths import Paths
from romconv.emit.asset import PLAIN_DICT_BYTES
from romconv.emit.lzma import lzma_decompress
from romconv.emit.text import c_symbol
from romconv.emit.write import banner
from romconv.plan.layout import define_name, ini_path, read_layouts, remote_layouts
from romconv.remote.bundle import BundleWriter, bundle_hash, write_bundle
from romconv.remote.catalog import NO_BLOBS, bundle_dirs, job_name, read_games


def _remote_plain(machines: Path, dirname: str, layout: str) -> dict[str, bool]:
    """<name> -> PLAIN flag of that layout's block in <dir>_arena_plan.h."""
    path = machines / dirname / f"{dirname}_arena_plan.h"
    if not path.exists():
        return {}
    text = path.read_text(encoding="utf-8")
    m = re.search(r"#(?:if|elif) defined\(" + define_name(layout) + r"\)\n(.*?)(?=#elif|#else)", text, re.S)
    if not m:
        raise ValueError(f"{path}: no block for {layout}")
    return {n: v == "1" for n, v in re.findall(r"#define (\w+)_PLAIN ([01])", m.group(1))}


def _assets(game: str, paths: Paths, layout: str) -> list[tuple[str, bytes, bytes, str, int]]:
    """(name, raw, stored, mode, full-dictionary bytes) of the game's dirs for one layout."""
    out = []
    for d in bundle_dirs(game):
        src = paths.assets / d
        if not src.is_dir():
            if game in NO_BLOBS:
                continue
            raise ValueError(f"no sidecars of dir {d} in {src} (convert it first)")
        plan = _remote_plain(paths.machines, d, layout)
        for meta_file in sorted(src.glob("*.json"), key=lambda p: p.name):
            name = meta_file.stem
            mode = json.loads(meta_file.read_bytes())["mode"]
            if mode == "PLANNED":
                if name not in plan:
                    raise ValueError(f"{name}: planned, but {d}_arena_plan.h has no block for {layout}")
                mode = "PLAIN" if plan[name] else "COMPRESSED"
            raw = (src / f"{name}.raw").read_bytes()
            full = (src / f"{name}.lz").read_bytes()
            stored = full if mode == "COMPRESSED" else (src / f"{name}.lz64").read_bytes()
            if mode == "PLAIN" and lzma_decompress(stored, PLAIN_DICT_BYTES) != raw:
                raise ValueError(f"{name}: 64 KB dictionary stream does not decode")
            out.append((name, raw, stored, mode, len(full)))
    return out


def _header(game: str, refs: dict[str, tuple[str, bytes, int, int]]) -> str:
    """refs = {layout: (hex name, digest, wire bytes, entry bytes)} in ini order."""
    sym = c_symbol(game)
    out = [
        banner(__file__).rstrip("\n"),
        '#include "../../net/bundle.h"',
        # nothing to download while every dir is in the app (amidar built in
        # keeps turtles' and scramble's assets, so turtles needs no bundle)
        "#if " + " || ".join(f"!defined(BUILTIN_{d.upper()})" for d in bundle_dirs(game))]
    for i, (layout, (hex_name, digest, wire, entry)) in enumerate(refs.items()):
        hash_c = ", ".join("0x%02x" % b for b in digest)
        out += [f"#{'if' if i == 0 else 'elif'} defined({define_name(layout)})",
                f"// GET /g/{hex_name}",
                f'static constexpr BundleRef {sym}_bundle = {{ {{ {hash_c} }}, {wire}u, {entry}u, "{game.upper()}" }};']
    out += [
        "#else",
        '#error "no bundle for this Arena layout: rerun romconv"',
        "#endif",
        f"#define {sym}_bundle_ref (&{sym}_bundle)",
        "#else",
        f"#define {sym}_bundle_ref nullptr",
        "#endif",
        ""]
    return "\n".join(out)


def _build(items) -> tuple[bytes, int, dict[str, int]]:
    """wire, entry bytes and the PLAIN dictionary report of one layout's items."""
    w = BundleWriter()
    plain_raw = plain64 = plain_full = 0
    for name, raw, stored, mode, full in items:
        w.add(name, raw, stored, mode)
        if mode == "PLAIN":
            plain_raw, plain64, plain_full = plain_raw + len(raw), plain64 + len(stored), plain_full + full
    wire = w.build()
    return wire, len(w.entry()), {"plain_raw": plain_raw, "plain64": plain64, "plain_full": plain_full}


def make_bundle(ctx: Context, game: str) -> dict[str, dict[str, int | str]] | None:
    """Write one bundle per _remote layout and <game>_bundle.h; report numbers per layout."""
    layouts = remote_layouts(read_layouts(ini_path(ctx.paths)))
    per_layout = {layout: _assets(game, ctx.paths, layout) for layout in layouts}
    if not any(per_layout.values()):
        # tables only (hero): nothing to download
        with ctx.header(f"{game}_bundle.h", machine=game, plan=False, remote=False) as f:
            f.write(banner(__file__) +
                    f"// no asset blobs: always built in\n#define {c_symbol(game)}_bundle_ref nullptr\n")
        return None

    refs = {}
    report = {}
    for layout, items in per_layout.items():
        wire, entry, plain = _build(items)
        hex_name = write_bundle(wire, ctx.paths.server)
        refs[layout] = (hex_name, bundle_hash(wire), len(wire), entry)
        report[layout] = {"hex": hex_name, "wire": len(wire), "entry": entry, **plain}
    with ctx.header(f"{game}_bundle.h", machine=game, plan=False, remote=False) as f:
        f.write(_header(game, refs))
    return report


def _convert(ctx: Context) -> None:
    requested = set(ctx.requested)
    games = [g for g, in_catalog in read_games(ctx.paths).items() if in_catalog and job_name(g) in requested]
    totals = {}  # per layout
    for game in games:
        try:
            r = make_bundle(ctx, game)
        except ValueError as e:
            raise ConversionError(f"{game}: {e}") from e
        if not r:
            ctx.log.info(f"  {game:12} no asset blobs, always built in")
            continue
        for layout, n in r.items():
            total = totals.setdefault(layout, {"wire": 0, "entry": 0, "plain_raw": 0, "plain64": 0, "plain_full": 0})
            for k in total:
                total[k] += n[k]
            ctx.log.info(f"  {game:12} {layout:12} wire {n['wire']:7} entry {n['entry']:7}  PLAIN raw {n['plain_raw']:7}"
                         f" 64K-dict {n['plain64']:7} full-dict {n['plain_full']:7}  g/{n['hex']}")
    for layout, total in totals.items():
        loss = total["plain64"] - total["plain_full"]
        ctx.log.info(f"  {layout}: total wire {total['wire']} entry {total['entry']}; PLAIN raw {total['plain_raw']},"
                     f" 64 KB dictionary {total['plain64']} vs full {total['plain_full']} (+{loss} bytes)")


JOB = Job(name="bundles", convert=_convert, deps=("catalog",), phase=2)
