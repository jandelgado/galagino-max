"""NEC MC-8123 decryption, port of MAME devices/cpu/z80/mc8123.cpp."""
from romconv.gfx.bits import BIT


def _bits(*b):
    v = 0
    for n in b:
        v |= 1 << n
    return v


# MAME bitswap<8>(val, b7, ..., b0): output bit 7 = input bit b7
def _bitswap(v, *b):
    out = 0
    for src in b:
        out = (out << 1) | BIT(v, src)
    return out


def _type0(val, param, swap):
    val = _bitswap(val, *((7, 5, 3, 1, 2, 0, 6, 4), (5, 3, 7, 2, 1, 0, 4, 6),
                         (0, 3, 4, 6, 7, 1, 5, 2), (0, 7, 3, 2, 6, 4, 1, 5))[swap])
    if BIT(param, 3) and BIT(val, 7):
        val ^= _bits(5, 3, 0)
    if BIT(param, 2) and BIT(val, 6):
        val ^= _bits(7, 2, 1)
    if BIT(val, 6):
        val ^= _bits(7)
    if BIT(param, 1) and BIT(val, 7):
        val ^= _bits(6)
    if BIT(val, 2):
        val ^= _bits(5, 0)
    val ^= _bits(4, 3, 1)
    if BIT(param, 2):
        val ^= _bits(5, 2, 0)
    if BIT(param, 1):
        val ^= _bits(7, 6)
    if BIT(param, 0):
        val ^= _bits(5, 0)
    if BIT(param, 0):
        val = _bitswap(val, 7, 6, 5, 1, 4, 3, 2, 0)
    return val


def _type1a(val, param, swap):
    val = _bitswap(val, *((4, 2, 6, 5, 3, 7, 1, 0), (6, 0, 5, 4, 3, 2, 1, 7),
                         (2, 3, 6, 1, 4, 0, 7, 5), (6, 5, 1, 3, 2, 7, 0, 4))[swap])
    if BIT(param, 2):
        val = _bitswap(val, 7, 6, 1, 5, 3, 2, 4, 0)
    if BIT(val, 1):
        val ^= _bits(0)
    if BIT(val, 6):
        val ^= _bits(3)
    if BIT(val, 7):
        val ^= _bits(6, 3)
    if BIT(val, 2):
        val ^= _bits(6, 3, 1)
    if BIT(val, 4):
        val ^= _bits(7, 6, 2)
    if BIT(val, 7) ^ BIT(val, 2):
        val ^= _bits(4)
    val ^= _bits(6, 3, 1, 0)
    if BIT(param, 3):
        val ^= _bits(7, 2)
    if BIT(param, 1):
        val ^= _bits(6, 3)
    if BIT(param, 0):
        val = _bitswap(val, 7, 6, 1, 4, 3, 2, 5, 0)
    return val


def _type1b(val, param, swap):
    val = _bitswap(val, *((1, 0, 3, 2, 5, 6, 4, 7), (2, 0, 5, 1, 7, 4, 6, 3),
                         (6, 4, 7, 2, 0, 5, 1, 3), (7, 1, 3, 6, 0, 2, 5, 4))[swap])
    if BIT(val, 2) and BIT(val, 0):
        val ^= _bits(7, 4)
    if BIT(val, 7):
        val ^= _bits(2)
    if BIT(val, 5):
        val ^= _bits(7, 2)
    if BIT(val, 1):
        val ^= _bits(5)
    if BIT(val, 6):
        val ^= _bits(1)
    if BIT(val, 4):
        val ^= _bits(6, 5)
    if BIT(val, 0):
        val ^= _bits(6, 2, 1)
    if BIT(val, 3):
        val ^= _bits(7, 6, 2, 1, 0)
    val ^= _bits(6, 4, 0)
    if BIT(param, 3):
        val ^= _bits(4, 1)
    if BIT(param, 2):
        val ^= _bits(7, 6, 3, 0)
    if BIT(param, 1):
        val ^= _bits(4, 3)
    if BIT(param, 0):
        val ^= _bits(6, 2, 1, 0)
    return val


def _type2a(val, param, swap):
    val = _bitswap(val, *((0, 1, 4, 3, 5, 6, 2, 7), (6, 3, 0, 5, 7, 4, 1, 2),
                         (1, 6, 4, 5, 0, 3, 7, 2), (4, 6, 7, 5, 2, 3, 1, 0))[swap])
    if BIT(val, 3) or (BIT(param, 1) and BIT(val, 2)):
        val = _bitswap(val, 6, 0, 7, 4, 3, 2, 1, 5)
    if BIT(val, 5):
        val ^= _bits(7)
    if BIT(val, 6):
        val ^= _bits(5)
    if BIT(val, 0):
        val ^= _bits(6)
    if BIT(val, 4):
        val ^= _bits(3, 0)
    if BIT(val, 1):
        val ^= _bits(2)
    val ^= _bits(7, 6, 5, 4, 1)
    if BIT(param, 2):
        val ^= _bits(4, 3, 2, 1, 0)
    if BIT(param, 3):
        if BIT(param, 0):
            val = _bitswap(val, 7, 6, 5, 3, 4, 1, 2, 0)
        else:
            val = _bitswap(val, 7, 6, 5, 1, 2, 4, 3, 0)
    elif BIT(param, 0):
        val = _bitswap(val, 7, 6, 5, 2, 1, 3, 4, 0)
    return val


def _type2b(val, param, swap):
    val = _bitswap(val, *((1, 3, 4, 6, 5, 7, 0, 2), (0, 1, 5, 4, 7, 3, 2, 6),
                         (3, 5, 4, 1, 6, 2, 0, 7), (5, 2, 3, 0, 4, 7, 6, 1))[swap])
    if BIT(val, 7) and BIT(val, 3):
        val ^= _bits(6, 4, 0)
    if BIT(val, 7):
        val ^= _bits(2)
    if BIT(val, 5):
        val ^= _bits(7, 3)
    if BIT(val, 1):
        val ^= _bits(5)
    if BIT(val, 4):
        val ^= _bits(7, 5, 3, 1)
    if BIT(val, 7) and BIT(val, 5):
        val ^= _bits(4, 0)
    if BIT(val, 5) and BIT(val, 1):
        val ^= _bits(4, 0)
    if BIT(val, 6):
        val ^= _bits(7, 5)
    if BIT(val, 3):
        val ^= _bits(7, 6, 5, 1)
    if BIT(val, 2):
        val ^= _bits(3, 1)
    val ^= _bits(7, 3, 2, 1)
    if BIT(param, 3):
        val ^= _bits(6, 3, 1)
    if BIT(param, 2):
        val ^= _bits(7, 6, 5, 3, 2, 1)
    if BIT(param, 1):
        val ^= _bits(7)
    if BIT(param, 0):
        val ^= _bits(5, 2)
    return val


def _type3a(val, param, swap):
    val = _bitswap(val, *((5, 3, 1, 7, 0, 2, 6, 4), (3, 1, 2, 5, 4, 7, 0, 6),
                         (5, 6, 1, 2, 7, 0, 4, 3), (5, 6, 7, 0, 4, 2, 1, 3))[swap])
    if BIT(val, 2):
        val ^= _bits(7, 5, 4)
    if BIT(val, 3):
        val ^= _bits(0)
    if BIT(param, 0):
        val = _bitswap(val, 7, 2, 5, 4, 3, 1, 0, 6)
    if BIT(val, 1):
        val ^= _bits(6, 0)
    if BIT(val, 3):
        val ^= _bits(4, 2, 1)
    if BIT(param, 3):
        val ^= _bits(4, 3)
    if BIT(val, 3):
        val = _bitswap(val, 5, 6, 7, 4, 3, 2, 1, 0)
    if BIT(val, 5):
        val ^= _bits(2, 1)
    val ^= _bits(6, 5, 4, 3)
    if BIT(param, 2):
        val ^= _bits(7)
    if BIT(param, 1):
        val ^= _bits(4)
    if BIT(param, 0):
        val ^= _bits(0)
    return val


def _type3b(val, param, swap):
    val = _bitswap(val, *((3, 7, 5, 4, 0, 6, 2, 1), (7, 5, 4, 6, 1, 2, 0, 3),
                         (7, 4, 3, 0, 5, 1, 6, 2), (2, 6, 4, 1, 3, 7, 0, 5))[swap])
    if BIT(val, 2):
        val ^= _bits(7)
    if BIT(val, 7):
        val = _bitswap(val, 7, 6, 3, 4, 5, 2, 1, 0)
    if BIT(param, 3):
        val ^= _bits(7)
    if BIT(val, 4):
        val ^= _bits(6)
    if BIT(val, 1):
        val ^= _bits(6, 4, 2)
    if BIT(val, 7) and BIT(val, 6):
        val ^= _bits(1)
    if BIT(val, 7):
        val ^= _bits(1)
    if BIT(param, 3):
        val ^= _bits(7)
    if BIT(param, 2):
        val ^= _bits(0)
    if BIT(param, 3):
        val = _bitswap(val, 4, 6, 3, 2, 5, 0, 1, 7)
    if BIT(val, 4):
        val ^= _bits(1)
    if BIT(val, 5):
        val ^= _bits(4)
    if BIT(val, 7):
        val ^= _bits(2)
    val ^= _bits(5, 3, 2)
    if BIT(param, 1):
        val ^= _bits(7)
    if BIT(param, 0):
        val ^= _bits(3)
    return val


_TYPES = (_type0, _type0, _type1a, _type1b, _type2a, _type2b, _type3a, _type3b)


def _decrypt_internal(val, key, opcode):
    key ^= 0xff
    if key == 0:
        return val
    k = [BIT(key, n) for n in range(8)]
    typ = (k[0] ^ k[2]) | (k[0] ^ k[1] ^ k[2] ^ k[4]) << 1 | (k[4] ^ k[5]) << 2
    swap = (k[0] ^ k[1]) | (k[2] ^ k[3]) << 1
    param = (k[0] | (k[0] ^ k[2] ^ k[3]) << 1 | (k[0] ^ k[1] ^ k[6]) << 2 |
             (k[1] ^ k[6] ^ k[7]) << 3)
    if not opcode:
        param ^= 1
        typ ^= 1
    return _TYPES[typ](val, param, swap)


def _decrypt(key, addr, val, opcode):
    tbl = _bitswap(addr, 15, 14, 13, 12, 11, 10, 8, 6, 4, 2, 1, 0)
    return _decrypt_internal(val, key[tbl | (0 if opcode else 0x1000)], opcode)


# MAME mc8123_device::decode() over romconv's main image: 0000-7fff fixed,
# then 16 KB banks that all run at 8000-bfff. Returns (data, opcodes).
def decode(rom, key):
    data = bytearray(len(rom))
    ops = bytearray(len(rom))
    for i, src in enumerate(rom):
        adr = i if i < 0x8000 else (i & 0x3fff) | 0x8000
        ops[i] = _decrypt(key, adr, src, True)
        data[i] = _decrypt(key, adr, src, False)
    return bytes(data), bytes(ops)
