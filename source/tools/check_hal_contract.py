#!/usr/bin/env python3
"""Checks that every name of hal/CONTRACT.md is defined in each backend directory.

Sections that are checked: Time, GPIO, Tasks and sync, Watchdog, LEDs, Audio, System, I2C.
A name counts as defined when its identifier occurs in a file of the backend directory.

  python3 tools/check_hal_contract.py [--hal DIR] [--backends esp,host,rp2350]
  python3 tools/check_hal_contract.py --selftest
"""
import argparse
import re
import sys
import tempfile
from pathlib import Path

_SECTIONS = ("Time", "GPIO", "Tasks and sync", "Watchdog", "LEDs", "Audio", "System", "I2C")
_SKIP = {
    "hal", "uint8_t", "uint16_t", "uint32_t", "int16_t", "int", "bool", "void", "const", "char",
    "static", "class", "struct", "typedef", "NULL", "ms", "led_color", "CRGB", "fill_solid",
    "nblend", "LED_BRIGHTNESS", "LED_PIN", "audio_config", "host", "esp", "rp2350", "ldots",
}
_IDENT = re.compile(r"\b[A-Za-z_][A-Za-z0-9_]*\b")


def _api_names(contract_text):
    """Subset that is an API symbol: followed by '(' , after class/struct, or ALL_CAPS."""
    names = set()
    section = None
    for line in contract_text.splitlines():
        header = re.match(r"##\s+(.*?)(?:\s+\(|$)", line)
        if header:
            section = header.group(1).strip()
            continue
        if section not in _SECTIONS:
            continue
        for span in re.findall(r"`([^`]*)`", line):
            span = re.sub(r"/\*.*?\*/", "", span)
            for m in re.finditer(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*\(", span):
                names.add(m.group(1))
            for m in re.finditer(r"\b(?:class|struct|typedef\s+\S+)\s+([A-Za-z_][A-Za-z0-9_]*)", span):
                names.add(m.group(1))
            for m in re.finditer(r"\b([A-Z][A-Z0-9_]{3,})\b", span):
                names.add(m.group(1))
    return {n for n in names if n not in _SKIP and n not in ("static", "void")}


def _missing(hal_dir, backends):
    hal_dir = Path(hal_dir)
    names = _api_names((hal_dir / "CONTRACT.md").read_text())
    out = {}
    for backend in backends:
        text = ""
        for f in sorted((hal_dir / backend).glob("*")):
            if f.suffix in (".h", ".cpp", ".c"):
                text += f.read_text()
        idents = set(_IDENT.findall(text))
        out[backend] = sorted(n for n in names if n not in idents)
    return out


def _selftest():
    with tempfile.TemporaryDirectory() as tmp:
        hal = Path(tmp)
        (hal / "CONTRACT.md").write_text(
            "## Time (`time.h`)\n- `uint32_t millis()`\n## GPIO\n- `void pinRead(int pin)`\n"
            "## Video (`video.h`)\n- `void begin()`\n")
        (hal / "a").mkdir()
        (hal / "b").mkdir()
        (hal / "a" / "x.h").write_text("uint32_t millis(); void pinRead(int);")
        (hal / "b" / "x.h").write_text("uint32_t millis();")
        got = _missing(hal, ["a", "b"])
        assert got == {"a": [], "b": ["pinRead"]}, got
    print("selftest ok")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--hal", default=str(Path(__file__).resolve().parent.parent / "src" / "hal"))
    ap.add_argument("--backends", default="esp,host,rp2350")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()
    if args.selftest:
        _selftest()
        return 0
    bad = False
    for backend, missing in _missing(args.hal, args.backends.split(",")).items():
        if missing:
            bad = True
            print("%s: missing %s" % (backend, ", ".join(missing)))
    if not bad:
        print("hal contract ok")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
