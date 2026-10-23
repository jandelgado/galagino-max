import ast
from pathlib import Path

ROMCONV = Path(__file__).resolve().parents[1]


def _imported_roots(path):
    """Top-level module names a file imports (absolute imports only)."""
    roots = set()
    for node in ast.walk(ast.parse(path.read_text(), str(path))):
        if isinstance(node, ast.Import):
            roots |= {alias.name.split(".")[0] for alias in node.names}
        elif isinstance(node, ast.ImportFrom) and node.level == 0 and node.module:
            roots.add(node.module.split(".")[0])
    return roots


def _offenders(directory, forbidden):
    files = sorted((ROMCONV / directory).rglob("*.py"))
    assert files, directory
    return [f"{path.relative_to(ROMCONV)}: {root}" for path in files
            for root in sorted(_imported_roots(path) & forbidden)]


def test_package_does_not_import_dev_or_tests():
    assert _offenders("romconv", {"dev", "tests"}) == []


def test_tests_do_not_import_dev():
    assert _offenders("tests", {"dev"}) == []


def test_layering_scan_sees_violations(tmp_path):
    bad = tmp_path / "bad.py"
    bad.write_text("import dev.golden\nfrom tests import conftest\nfrom . import x\nimport os\n")
    assert _imported_roots(bad) == {"dev", "tests", "os"}
