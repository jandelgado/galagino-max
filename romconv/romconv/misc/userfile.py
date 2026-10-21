import hashlib

from romconv.core.context import Context
from romconv.core.errors import ConversionError


def read_user_file(ctx: Context, name: str, sha1: str | None, hint: str) -> bytes:
    """A user-supplied file of romszip/ that is no zip member (HVSC SID tunes)."""
    path = ctx.zip_path(name)
    if not path.is_file():
        raise ConversionError(f"missing {path} ({hint})")

    data = path.read_bytes()
    if sha1 is not None and hashlib.sha1(data).hexdigest() != sha1:
        raise ConversionError(f"{path}: SHA-1 mismatch, expected {sha1}")
    return data
