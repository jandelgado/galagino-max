"""Unified-diff patch applier via patch-ng.

No system `patch` binary needed, so conversion works on Windows.
"""

import os

import patch_ng


def apply_patch(patch_file, dest_dir):
  patch_set = patch_ng.fromfile(os.path.abspath(patch_file))
  if not patch_set:
    raise RuntimeError(f"Failed to parse patch file: {patch_file}")

  if not patch_set.apply(strip=1, root=os.path.abspath(dest_dir)):
    raise RuntimeError(f"Failed to apply patch {patch_file} to {dest_dir}")
