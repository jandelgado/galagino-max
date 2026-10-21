"""Dev tools of the Marble model: put romconv/ on sys.path and load the ROM set.

The converter reads the ROMs through its Context; the tools read romszip/ directly. Importing
this module loads and decodes marble.zip and atarisy1.zip (a few seconds)."""
import hashlib
import sys
import zipfile
from pathlib import Path

ROMCONV = Path(__file__).resolve().parents[3]
REPO = ROMCONV.parent
ROMSZIP = REPO / "romszip"

if str(ROMCONV) not in sys.path:
    sys.path.insert(0, str(ROMCONV))

from romconv.games.marble import data as md  # noqa: E402
from romconv.games.marble.sim import marble_sim  # noqa: E402


def read_rom(name, sha1, zip_name):
    """Member of a zip in romszip/, checked against its SHA-1."""
    with zipfile.ZipFile(ROMSZIP / zip_name) as z:
        data = z.read(name)
    if hashlib.sha1(data).hexdigest() != sha1:
        sys.exit(f"{zip_name}: {name}: SHA-1 mismatch (expected {sha1})")
    return data


ROM = md.load(read_rom)
SIM_ROM = marble_sim.make_rom(ROM)
