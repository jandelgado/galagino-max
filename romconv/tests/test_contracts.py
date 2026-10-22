"""Static contracts of the build package (romconv/romconv): what a converter may not do.

AST scans, so a violation fails with its file and line; the import test runs the real imports."""
import ast
import os
import subprocess
import sys
from pathlib import Path

import pytest

from romconv import registry
from romconv.core.errors import ConversionError
from romconv.core.job import Job

ROMCONV = Path(__file__).resolve().parents[1]
PACKAGE = ROMCONV / "romconv"
FILES = sorted(PACKAGE.rglob("*.py"))

FORBIDDEN_CALLS = {
    "sys.exit": "sys.exit: raise ConversionError",
    "exit": "exit: raise ConversionError",
    "quit": "quit: raise ConversionError",
    "fatal": "fatal: raise ConversionError",
    "os.chdir": "os.chdir: paths come from Context.paths",
    "os.getenv": "os.getenv: parameters, not environment",
    "os.putenv": "os.putenv: parameters, not environment",
    "os.unsetenv": "os.unsetenv: parameters, not environment",
}
FORBIDDEN_ATTRS = {
    "os.environ": "os.environ: parameters, not environment",
    "sys.path": "sys.path: imports are package-relative",
}
FORBIDDEN_FROM_IMPORTS = {
    ("sys", "exit"), ("sys", "path"), ("os", "environ"), ("os", "getenv"), ("os", "putenv"),
    ("os", "unsetenv"), ("os", "chdir"),
}
OUTPUT_STREAMS = {"sys.stdout", "sys.stderr"}
MUTABLE_CALLS = {"list", "dict", "set", "bytearray", "defaultdict", "OrderedDict", "Counter", "deque"}


def _parse(path):
    return ast.parse(path.read_text(), str(path))


def _where(path, node):
    return f"{path.relative_to(ROMCONV)}:{node.lineno}"


def _dotted(node):
    """'a.b.c' for a Name/Attribute chain, else None."""
    parts = []
    while isinstance(node, ast.Attribute):
        parts.append(node.attr)
        node = node.value
    if not isinstance(node, ast.Name):
        return None
    return ".".join([node.id, *reversed(parts)])


def _import_time_statements(body):
    """Statements that run on import: module and class bodies, not function bodies."""
    for stmt in body:
        yield stmt
        if isinstance(stmt, ast.ClassDef):
            yield from _import_time_statements(stmt.body)
        elif not isinstance(stmt, (ast.FunctionDef, ast.AsyncFunctionDef)):
            for field in ("body", "orelse", "finalbody"):
                yield from _import_time_statements(getattr(stmt, field, []))
            for handler in getattr(stmt, "handlers", []):
                yield from _import_time_statements(handler.body)


def _imported_names(tree):
    names = set()
    for node in ast.walk(tree):
        if isinstance(node, ast.Import):
            names |= {(a.asname or a.name).split(".")[0] for a in node.names}
        elif isinstance(node, ast.ImportFrom):
            names |= {a.asname or a.name for a in node.names}
    return names


def _forbidden_calls(path):
    hits = []
    tree = _parse(path)
    for node in ast.walk(tree):
        if isinstance(node, ast.Call):
            name = _dotted(node.func)
            if name in FORBIDDEN_CALLS:
                hits.append(f"{_where(path, node)} {FORBIDDEN_CALLS[name]}")
            if name == "print":
                file = next((k.value for k in node.keywords if k.arg == "file"), None)
                if file is None or _dotted(file) in OUTPUT_STREAMS:
                    hits.append(f"{_where(path, node)} print: use ctx.log, or print(..., file=<writer>)")
        elif isinstance(node, ast.Attribute):
            name = _dotted(node)
            if name in FORBIDDEN_ATTRS:
                hits.append(f"{_where(path, node)} {FORBIDDEN_ATTRS[name]}")
        elif isinstance(node, ast.ImportFrom) and node.level == 0:
            for alias in node.names:
                if (node.module, alias.name) in FORBIDDEN_FROM_IMPORTS:
                    hits.append(f"{_where(path, node)} from {node.module} import {alias.name}")
    return hits


def _module_mutation(path):
    hits = []
    tree = _parse(path)
    imported = _imported_names(tree)
    for node in ast.walk(tree):
        if isinstance(node, ast.Global):
            hits.append(f"{_where(path, node)} global {', '.join(node.names)}")
    for stmt in _import_time_statements(tree.body):
        targets = []
        if isinstance(stmt, ast.Assign):
            targets = stmt.targets
        elif isinstance(stmt, (ast.AugAssign, ast.AnnAssign)):
            targets = [stmt.target]
        elif isinstance(stmt, ast.Delete):
            targets = stmt.targets
        for target in targets:
            base = target
            while isinstance(base, (ast.Attribute, ast.Subscript)):
                base = base.value
            if base is not target and isinstance(base, ast.Name) and base.id in imported:
                hits.append(f"{_where(path, stmt)} assigns into imported {base.id}")
    return hits


def _mutable_state(path):
    """Import-time assignments of a mutable container: state that outlives a job."""
    hits = []
    for stmt in _import_time_statements(_parse(path).body):
        if not isinstance(stmt, (ast.Assign, ast.AnnAssign)) or stmt.value is None:
            continue
        value = stmt.value
        call = _dotted(value.func) if isinstance(value, ast.Call) else None
        if isinstance(value, (ast.List, ast.Dict, ast.Set, ast.ListComp, ast.DictComp, ast.SetComp)) \
                or (call or "").split(".")[-1] in MUTABLE_CALLS:
            hits.append(f"{_where(path, stmt)} mutable container; use tuple, frozenset or MappingProxyType")
    return hits


def _all(check):
    return [hit for path in FILES for hit in check(path)]


def test_package_files_found():
    assert len(FILES) > 100


def test_no_forbidden_calls():
    assert _all(_forbidden_calls) == []


def test_no_module_level_mutation():
    assert _all(_module_mutation) == []


def test_no_mutable_module_or_class_state():
    assert _all(_mutable_state) == []


class _Probe:
    """A file the scans can read: just its source."""

    def __init__(self, source):
        self.source = source

    def read_text(self):
        return self.source

    def relative_to(self, root):
        return Path("probe.py")


@pytest.mark.parametrize("source, count", [
    ("import sys\nsys.exit(1)\n", 1),
    ("from sys import exit\nexit(1)\nquit()\n", 3),
    ("import os\nx = os.environ['A']\nos.chdir('.')\n", 2),
    ("import sys\nsys.path.insert(0, '.')\n", 1),
    ("print('x')\nprint('x', file=sys.stderr)\nprint('x', file=f)\n", 2),
    ("def f():\n    fatal('x')\n", 1),
])
def test_forbidden_call_scan_sees_violations(source, count):
    assert len(_forbidden_calls(_Probe(source))) == count


def test_mutation_scans_see_violations():
    assert len(_module_mutation(_Probe("import m\nm.X = 1\ndef f():\n    global G\n"))) == 2
    assert len(_module_mutation(_Probe("import m\ndef f():\n    m.X = 1\n"))) == 0
    assert len(_mutable_state(_Probe("A = []\nB = {}\nC = (1, 2)\nclass K:\n    D = set()\n"))) == 3


# private names


def _module_name(path):
    parts = list(path.relative_to(ROMCONV).with_suffix("").parts)
    if parts[-1] == "__init__":
        parts.pop()
    return ".".join(parts)


def _public_names(tree):
    names = {}
    for node in tree.body:
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)):
            names[node.name] = node.lineno
        elif isinstance(node, (ast.Assign, ast.AnnAssign)):
            targets = node.targets if isinstance(node, ast.Assign) else [node.target]
            for target in targets:
                names.update({n.id: node.lineno for n in ast.walk(target) if isinstance(n, ast.Name)})
    return {name: line for name, line in names.items() if not name.startswith("_")}


def _imported_from(path, modules):
    """(module, name) pairs another file takes from the package: `from m import n`, `m.n`, `import m`
    attribute access, star imports (every name)."""
    tree = _parse(path)
    me = _module_name(path)
    found, aliases = set(), {}
    for node in ast.walk(tree):
        if isinstance(node, ast.Import):
            for a in node.names:
                aliases[a.asname or a.name.split(".")[0]] = a.name if a.asname else a.name.split(".")[0]
        elif isinstance(node, ast.ImportFrom):
            base = node.module or ""
            if node.level:
                parts = me.split(".")
                keep = len(parts) - node.level + (1 if path.name == "__init__.py" else 0)
                base = ".".join(parts[:keep] + ([node.module] if node.module else []))
            for a in node.names:
                if f"{base}.{a.name}" in modules:
                    aliases[a.asname or a.name] = f"{base}.{a.name}"
                elif a.name == "*":
                    found |= {(base, n) for n in _PUBLIC.get(base, ())}
                elif base != me:
                    found.add((base, a.name))
    for node in ast.walk(tree):
        if not isinstance(node, ast.Attribute):
            continue
        chain = [node.attr]
        value = node.value
        while isinstance(value, ast.Attribute):
            chain.append(value.attr)
            value = value.value
        if isinstance(value, ast.Name) and value.id in aliases:
            parts = [*aliases[value.id].split("."), *reversed(chain)]
            for i in range(len(parts) - 1, 0, -1):
                if ".".join(parts[:i]) in modules:
                    found.add((".".join(parts[:i]), parts[i]))
                    break
    return found


_PUBLIC = {_module_name(path): _public_names(_parse(path)) for path in FILES}


def test_public_names_are_imported_elsewhere():
    """Public = imported by another module (package, dev, tests, convert.py); the rest starts with _."""
    users = [*sorted((ROMCONV / "dev").rglob("*.py")), *sorted((ROMCONV / "tests").rglob("*.py")),
             ROMCONV / "convert.py", *FILES]
    used = set()
    for path in users:
        used |= _imported_from(path, set(_PUBLIC))
    unused = [f"{module}:{line} {name}" for module, names in sorted(_PUBLIC.items())
              for name, line in names.items() if (module, name) not in used]
    assert unused == [], "public but imported nowhere, rename to _name:\n" + "\n".join(unused)


_IMPORT_ALL = """
import importlib, sys
modules = sys.argv[1:]
def audit(event, args):
    if event == "open":
        path, mode, flags = args
        writing = (mode and any(c in str(mode) for c in "wax+")) or (flags & (os_flags))
        if writing:
            raise RuntimeError(f"write at import: {path}")
    elif event in ("os.mkdir", "os.remove", "os.rename", "os.rmdir", "os.chdir", "subprocess.Popen",
                   "os.system", "shutil.rmtree"):
        raise RuntimeError(f"{event} at import: {args}")
import os
os_flags = os.O_WRONLY | os.O_RDWR | os.O_CREAT | os.O_APPEND | os.O_TRUNC
sys.addaudithook(audit)
for name in modules:
    importlib.import_module(name)
"""


def _module_names():
    names = []
    for path in FILES:
        parts = list(path.relative_to(ROMCONV).with_suffix("").parts)
        if parts[-1] == "__init__":
            parts.pop()
        names.append(".".join(parts))
    return names


def test_import_has_no_side_effects(tmp_path):
    env = {**os.environ, "PYTHONPATH": str(ROMCONV), "PYTHONDONTWRITEBYTECODE": "1"}
    done = subprocess.run([sys.executable, "-c", _IMPORT_ALL, *_module_names()], cwd=tmp_path, env=env,
                          capture_output=True, text=True)
    assert done.returncode == 0, done.stderr
    assert done.stdout == "" and done.stderr == ""
    assert list(tmp_path.iterdir()) == []


# registry


def test_every_job_name_unique():
    names = [job.name for job in registry.ALL]
    assert len(names) == len(set(names)), sorted(n for n in names if names.count(n) > 1)
    registry.validate(registry.ALL)


def test_validate_rejects_duplicates_and_bad_phases():
    job = Job("a", lambda ctx: None)
    with pytest.raises(ConversionError, match="duplicate job name 'a'"):
        registry.validate((job, job))
    with pytest.raises(ConversionError, match="phase"):
        registry.validate((Job("b", lambda ctx: None, phase=0),))


LEGACY_NAMES = frozenset("""
    logos z80 1942 alibaba amidar anteater asteroids bagman bnj bombjack centipede millipede btime circusc
    commando crush digdug digdug2 dkong dkong3 dkongjr eyes frogger galaga galaxian gaplus gyruss invaders
    ladybug lizwiz llander m6502 mappy mario mooncresta motorace mrdo mrtnt mspacman nibbler pacman pbaction
    pengo phoenix pooyan roadfighter rocnrope scramble scregg starforce baluba supercobra theglob timeplt
    todruaga tp84 turtles tutankhm vanvan vanguard fantasy xevious zaxxon szaxxon thepit bublbobl atetris ccastles
    bzone tempest spacduel gravitar bwidow berzerk starwars missile mpatrol starjack regulus upndown raflesia
    swat cbdash phozon gberet hero gng marble defender robotron joust sinistar 1943 gorf solomon flicky
    pitfall2 mrviking imsorry wboy fourdwarrio hvymetal sxevious mikie vulgus cavenger gardia pleiads
    arkanoid yiear trackfld tron dotron tapper shollow motos jungleh megazone elevator mhavoc choplift
    tokisens
""".split())
NEW_NAMES = frozenset({"namco54", "system1_palette", "williams_palette", "pokey_poly", "unpack_fixture",
                       "catalog", "bundles", "docastle"})


def test_cli_names_unchanged():
    assert len(LEGACY_NAMES) == 122
    assert {job.name for job in registry.ALL} == LEGACY_NAMES | NEW_NAMES
