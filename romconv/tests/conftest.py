import logging
import zipfile

import pytest

from romconv.core.paths import Paths


@pytest.fixture
def paths(tmp_path):
    return Paths(tmp_path)


@pytest.fixture
def log():
    return logging.Logger("test")


@pytest.fixture
def make_zip(paths):
    """make_zip("g.zip", {"a.bin": b"..."}) writes into paths.romszip."""

    def _make(name, members):
        paths.romszip.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(paths.romszip / name, "w") as z:
            for member, data in members.items():
                z.writestr(member, data)

    return _make
