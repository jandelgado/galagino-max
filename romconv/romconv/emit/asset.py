# Asset<T> C-source emitter shared by the converters.
#
# Emits the array plus a constexpr FlashAsset `<name>_blob` describing it. The
# machine owns an Asset member built from the blob, so the unpack cache
# dies with the machine. Menu logos wrap the blob in a Logo.
#
# Headers written into source/src/machines/<dir>/ also get a remote form
# (ASSETS_REMOTE build without the game built in): the blob carries the asset
# id, Asset resolves it in the mounted bundle. The assets are registered in
# the writer's manifest, which feeds the bundles job.
#
#   #if defined(ASSETS_REMOTE) && !defined(BUILTIN_<DIR>)
#   <name>_blob = { nullptr, ..., <id> }      remote
#   #else
#   <name>_data / <name>_packed, <name>_blob  built in (today's form)
#   #endif
import re
from collections.abc import Sequence

from romconv.core.header import HeaderWriter
from romconv.core.manifest import Asset
from romconv.emit.lzma import lzma_compress
from romconv.emit.text import WIDTH, body_values, flat_body, hex_block, raw_bytes
from romconv.remote.bundle import asset_id

# Dictionary of PLAIN assets in remote bundles: the loader decodes them into
# a dictionary buffer in Arena (source/src/net/bundle_unpack.h).
PLAIN_DICT_BYTES = 65536


def _inner_elements(inner_dims: str) -> int:
    n = 1
    for d in re.findall(r"\d+", inner_dims):
        n *= int(d)
    return n


def _remote_open(f: HeaderWriter, rdir: str, name: str, ctype: str, inner_dims: str, count: int,
                 mode: str, raw: bytes, packed: bytes, hint: str | None) -> None:
    """Remote blob(s) and the #else of the built-in form; mode PLANNED: both
    blobs behind <name>_PLAIN from the arena plan header. Registers the asset."""
    t = f"{ctype}{inner_dims}"
    aid = asset_id(name)
    plain = f"static constexpr FlashAsset<{t}, PLAIN> {name}_blob = {{ nullptr, {count}, 0x{aid:08X}u }};"
    block = f"{name}_BLOCK" if mode == "PLANNED" else "ARENA_ANY"
    comp = (f"static constexpr FlashAsset<{t}, COMPRESSED> {name}_blob = "
            f"{{ nullptr, {len(packed)}, {count}, {block}, 0x{aid:08X}u }};")
    print(f"#if defined(ASSETS_REMOTE) && !defined(BUILTIN_{rdir.upper()})", file=f)
    if mode == "PLANNED":
        print(f"#if {name}_PLAIN\n{plain}\n#else\n{comp}\n#endif", file=f)
    else:
        print(plain if mode == "PLAIN" else comp, file=f)
    print("#else", file=f)
    lz64 = None if mode == "COMPRESSED" else lzma_compress(raw, PLAIN_DICT_BYTES)
    f.manifest.add(Asset(dir=rdir, name=name, ctype=ctype, inner=inner_dims, count=count, mode=mode,
                         raw=raw, packed=packed, lz64=lz64, hint=hint))


def _remote_close(f: HeaderWriter, rdir: str | None) -> None:
    if rdir:
        print("#endif", file=f)


def _emit_planned(f: HeaderWriter, game: str, name: str, ctype: str, inner_dims: str, count: int,
                  raw: bytes, array_body: str, hint: str, rdir: str | None) -> None:
    """Both forms, picked by <name>_PLAIN from <game>_arena_plan.h
    (plan.arena, also defines <name>_asset), plus the marker the plan
    reads. hint: the converter's own mode, the tie-break."""
    packed = lzma_compress(raw)
    ratio = 100.0 * (1.0 - len(packed) / len(raw)) if raw else 0.0
    t = f"{ctype}{inner_dims}"
    print('#include "../../emulation/asset.h"', file=f)
    print(f'#include "{game}_arena_plan.h"', file=f)
    print(f"// arena-asset {name} {len(raw)} {WIDTH[ctype]} {hint} {t}", file=f)
    if rdir:
        _remote_open(f, rdir, name, ctype, inner_dims, count, "PLANNED", raw, packed, hint)
    print(f"#if {name}_PLAIN", file=f)
    # aligned like an Arena copy: readers may load wider words (marble_tables)
    print(f"alignas(4) static const {ctype} {name}_data[]{inner_dims} = {{\n{array_body}\n}};", file=f)
    print(f"static constexpr FlashAsset<{t}, PLAIN> {name}_blob = {{ {name}_data, {count} }};", file=f)
    print("#else", file=f)
    print(f"// {name}: {len(raw)} -> {len(packed)} bytes ({ratio:.1f}% smaller)", file=f)
    print(f"static const unsigned char {name}_packed[] = {{\n  " + hex_block(packed) + "\n};", file=f)
    print(f"static constexpr FlashAsset<{t}, COMPRESSED> {name}_blob = {{ {name}_packed, sizeof({name}_packed), {count}, {name}_BLOCK }};", file=f)
    print("#endif", file=f)
    _remote_close(f, rdir)


def emit_compressed(f: HeaderWriter, name: str, ctype: str, inner_dims: str, count: int,
                    flat_values: Sequence[int], plan: bool = True, remote: bool = True) -> None:
    """Write an LZMA-compressed FlashAsset<ctype inner_dims> definition.

    flat_values: ints in C initializer (row-major) order.
    f.plan_game set and plan: planned form, both PLAIN and COMPRESSED.
    remote: remote form too, if f has a remote dir (not for logos).
    """
    raw = raw_bytes(ctype, flat_values)
    rdir = f.remote_dir if remote else None
    game = f.plan_game if plan else None
    if game:
        _emit_planned(f, game, name, ctype, inner_dims, count, raw, flat_body(ctype, flat_values),
                      "COMPRESSED", rdir)
        return
    packed = lzma_compress(raw)
    ratio = 100.0 * (1.0 - len(packed) / len(raw)) if raw else 0.0

    print('#include "../../emulation/asset.h"', file=f)
    if rdir:
        _remote_open(f, rdir, name, ctype, inner_dims, count, "COMPRESSED", raw, packed, None)
    print(f"// {name}: {len(raw)} -> {len(packed)} bytes ({ratio:.1f}% smaller)", file=f)
    print(f"static const unsigned char {name}_packed[] = {{\n  " + hex_block(packed) + "\n};", file=f)
    print(f"static constexpr FlashAsset<{ctype}{inner_dims}, COMPRESSED> {name}_blob = {{ {name}_packed, sizeof({name}_packed), {count} }};", file=f)
    _remote_close(f, rdir)


def emit_plain(f: HeaderWriter, name: str, ctype: str, inner_dims: str, count: int, array_body: str,
               plan: bool = True, remote: bool = True) -> None:
    """Write a plain (flash-resident) FlashAsset<ctype inner_dims> definition.

    array_body: formatted `{ ... }` initializer text from the dump_* helpers.
    f.plan_game set and plan: planned form, values parsed back from the body.
    remote: remote form too, if f has a remote dir.
    """
    rdir = f.remote_dir if remote else None
    game = f.plan_game if plan else None
    if game:
        values = body_values(array_body, count * _inner_elements(inner_dims))
        _emit_planned(f, game, name, ctype, inner_dims, count, raw_bytes(ctype, values), array_body, "PLAIN", rdir)
        return
    print('#include "../../emulation/asset.h"', file=f)
    if rdir:
        # count is not always the byte count (ADPCM: samples), so take all values
        raw = raw_bytes(ctype, body_values(array_body))
        _remote_open(f, rdir, name, ctype, inner_dims, count, "PLAIN", raw, lzma_compress(raw), None)
    print(f"static const {ctype} {name}_data[]{inner_dims} = {{\n{array_body}\n}};", file=f)
    print(f"static constexpr FlashAsset<{ctype}{inner_dims}, PLAIN> {name}_blob = {{ {name}_data, {count} }};", file=f)
    _remote_close(f, rdir)
