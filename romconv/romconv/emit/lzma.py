import lzma

# Raw LZMA1 stream, decoded on the ESP32 by LzmaDec into the Arena. lc=lp=0
# keeps the decoder's probability table at 5.5 KB (free lc/lp: up to 28 KB,
# saves only ~5 KB flash over all assets). pb does not change decoder RAM,
# so it is searched per asset.
_PB_RANGE = range(5)


def _props_byte(lc: int, lp: int, pb: int) -> int:
    return (pb * 5 + lp) * 9 + lc


def lzma_compress(raw: bytes, dict_size: int | None = None) -> bytes:
    """Properties byte + raw LZMA1 stream (with end marker), smallest pb.
    dict_size None: the whole asset (decoded in place)."""
    dict_bytes = max(len(raw), 4096)
    if dict_size:
        dict_bytes = min(dict_size, dict_bytes)
    best = None
    for pb in _PB_RANGE:
        filters = [{"id": lzma.FILTER_LZMA1, "preset": 9 | lzma.PRESET_EXTREME,
                    "dict_size": dict_bytes, "lc": 0, "lp": 0, "pb": pb}]
        stream = lzma.compress(raw, format=lzma.FORMAT_RAW, filters=filters)
        if best is None or len(stream) < len(best):
            best = bytes([_props_byte(0, 0, pb)]) + stream
    if lzma_decompress(best) != raw:
        raise RuntimeError("lzma round-trip mismatch")
    return best


def lzma_decompress(packed: bytes, dict_size: int = 1 << 22) -> bytes:
    """Inverse of lzma_compress(). dict_size: the dictionary the stream is
    decoded with (the default covers the largest asset)."""
    props = packed[0]
    lc, rest = props % 9, props // 9
    lp, pb = rest % 5, rest // 5
    filters = [{"id": lzma.FILTER_LZMA1, "dict_size": dict_size, "lc": lc, "lp": lp, "pb": pb}]
    return lzma.decompress(packed[1:], format=lzma.FORMAT_RAW, filters=filters)
