"""Snapshot and compare all generated conversion output.

    python -m dev.golden run <label> [-- <convert args>]
    python -m dev.golden diff <a> <b>

Snapshots: romconv/build/golden/<label>/ (mirror of repo paths + index.json).
"""

import argparse
import difflib
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
from itertools import islice
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
ROMCONV = REPO / "romconv"
GOLDEN = ROMCONV / "build" / "golden"

# Generated files live under these roots (repo relative).
ROOTS = (
    "source/src/machines",
    "source/src/catalog.h",
    "source/src/emulation/pokey_poly.h",
    "source/src/cpus",
    "source/test",
    "galagino-server/data/g",
    "romconv/build/assets",
)
# Taken whole (every file), deleted whole before a run.
WHOLE = ("galagino-server/data/g", "romconv/build/assets")
# Also removed before a run.
CLEAN_DIRS = ("romconv/build/assets", "galagino-server/data/g")

DIFF_LINES = 40


def _ignored_files() -> list[str]:
    """Files under ROOTS that .gitignore ignores (read-only git query).

    Uses a scratch git dir with REPO as work tree: only the .gitignore files
    of the tree count (not a parent repo's info/exclude or global excludes,
    which matter when REPO is a workspace nested in another checkout)."""
    roots = [r for r in ROOTS if r not in WHOLE]
    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run(["git", "init", "-q", tmp], check=True)
        out = subprocess.run(
            ["git", f"--git-dir={tmp}/.git", f"--work-tree={REPO}", "-c", "core.excludesFile=/dev/null",
             "ls-files", "--others", "--ignored", "--exclude-standard", "-z", *roots],
            cwd=REPO, check=True, capture_output=True).stdout
    return [p for p in out.decode().split("\0") if p]


def _generated() -> list[str]:
    files = set(_ignored_files())
    for whole in WHOLE:
        base = REPO / whole
        if base.is_dir():
            files.update(p.relative_to(REPO).as_posix() for p in base.rglob("*") if p.is_file())
    return sorted(p for p in files if not p.endswith("~"))


def _run(label: str, convert_args: list[str]) -> int:
    dest = GOLDEN / label
    files = _generated()
    print(f"deleting {len(files)} generated files")
    for rel in files:
        (REPO / rel).unlink()
    for d in CLEAN_DIRS:
        shutil.rmtree(REPO / d, ignore_errors=True)

    result = subprocess.run([sys.executable, "convert.py", *convert_args], cwd=ROMCONV)
    if result.returncode != 0:
        print(f"convert.py exited with code {result.returncode}", file=sys.stderr)
        return 1

    files = _generated()
    shutil.rmtree(dest, ignore_errors=True)
    index = {}
    for rel in files:
        target = dest / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(REPO / rel, target)
        index[rel] = hashlib.sha256(target.read_bytes()).hexdigest()
    (dest / "index.json").write_text(json.dumps(index, indent=1, sort_keys=True) + "\n")
    print(f"snapshot {label}: {len(index)} files")
    return 0


def _load(label: str) -> dict[str, str]:
    path = GOLDEN / label / "index.json"
    if not path.is_file():
        raise SystemExit(f"no snapshot {label!r} ({path})")
    return json.loads(path.read_text())


def _text_diff(a: str, b: str, rel: str) -> list[str]:
    try:
        old = (GOLDEN / a / rel).read_text().splitlines()
        new = (GOLDEN / b / rel).read_text().splitlines()
    except UnicodeDecodeError:
        return ["  (binary)"]
    diff = difflib.unified_diff(old, new, f"{a}/{rel}", f"{b}/{rel}", lineterm="", n=1)
    return list(islice(diff, DIFF_LINES))


def _diff(a: str, b: str) -> int:
    ia, ib = _load(a), _load(b)
    missing = sorted(set(ia) - set(ib))
    extra = sorted(set(ib) - set(ia))
    changed = sorted(p for p in set(ia) & set(ib) if ia[p] != ib[p])
    for title, paths in (("missing", missing), ("extra", extra)):
        for p in paths:
            print(f"{title}: {p}")
    for p in changed:
        print(f"changed: {p}")
        for line in _text_diff(a, b, p):
            print(f"  {line}")
    if not (missing or extra or changed):
        print("identical")
        return 0
    return 1


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(prog="dev.golden")
    sub = parser.add_subparsers(dest="cmd", required=True)
    run = sub.add_parser("run", help="clean, convert, snapshot")
    run.add_argument("label")
    run.add_argument("convert_args", nargs=argparse.REMAINDER, help="after --: passed to convert.py")
    diff = sub.add_parser("diff", help="compare two snapshots")
    diff.add_argument("a")
    diff.add_argument("b")
    args = parser.parse_args(argv)
    if args.cmd == "run":
        extra = args.convert_args
        if extra and extra[0] == "--":
            extra = extra[1:]
        return _run(args.label, extra)
    return _diff(args.a, args.b)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
