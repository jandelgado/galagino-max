"""Unified-diff patch applier via patch-ng.

No system `patch` binary needed, so conversion works on Windows."""
from pathlib import Path

import patch_ng

from romconv.core.errors import ConversionError


def apply_patch(patch_file: Path, dest_dir: Path) -> None:
    patch_set = patch_ng.fromfile(str(patch_file.resolve()))
    if not patch_set:
        raise ConversionError(f"failed to parse patch file: {patch_file}")

    if not patch_set.apply(strip=1, root=str(dest_dir.resolve())):
        raise ConversionError(f"failed to apply patch {patch_file} to {dest_dir}")
