"""NEC MC-8123 decryption, port of MAME devices/cpu/z80/mc8123.cpp."""


def bit(v, n):
    return (v >> n) & 1


def bits(*b):
    v = 0
    for n in b:
        v |= 1 << n
    return v


# MAME bitswap<8>(val, b7, ..., b0): output bit 7 = input bit b7
def bitswap(v, *b):
    out = 0
    for src in b:
        out = (out << 1) | bit(v, src)
    return out


def type0(val, param, swap):
    val = bitswap(val, *((7, 5, 3, 1, 2, 0, 6, 4), (5, 3, 7, 2, 1, 0, 4, 6),
                         (0, 3, 4, 6, 7, 1, 5, 2), (0, 7, 3, 2, 6, 4, 1, 5))[swap])
    if bit(param, 3) and bit(val, 7):
        val ^= bits(5, 3, 0)
    if bit(param, 2) and bit(val, 6):
        val ^= bits(7, 2, 1)
    if bit(val, 6):
        val ^= bits(7)
    if bit(param, 1) and bit(val, 7):
        val ^= bits(6)
    if bit(val, 2):
        val ^= bits(5, 0)
    val ^= bits(4, 3, 1)
    if bit(param, 2):
        val ^= bits(5, 2, 0)
    if bit(param, 1):
        val ^= bits(7, 6)
    if bit(param, 0):
        val ^= bits(5, 0)
    if bit(param, 0):
        val = bitswap(val, 7, 6, 5, 1, 4, 3, 2, 0)
    return val


def type1a(val, param, swap):
    val = bitswap(val, *((4, 2, 6, 5, 3, 7, 1, 0), (6, 0, 5, 4, 3, 2, 1, 7),
                         (2, 3, 6, 1, 4, 0, 7, 5), (6, 5, 1, 3, 2, 7, 0, 4))[swap])
    if bit(param, 2):
        val = bitswap(val, 7, 6, 1, 5, 3, 2, 4, 0)
    if bit(val, 1):
        val ^= bits(0)
    if bit(val, 6):
        val ^= bits(3)
    if bit(val, 7):
        val ^= bits(6, 3)
    if bit(val, 2):
        val ^= bits(6, 3, 1)
    if bit(val, 4):
        val ^= bits(7, 6, 2)
    if bit(val, 7) ^ bit(val, 2):
        val ^= bits(4)
    val ^= bits(6, 3, 1, 0)
    if bit(param, 3):
        val ^= bits(7, 2)
    if bit(param, 1):
        val ^= bits(6, 3)
    if bit(param, 0):
        val = bitswap(val, 7, 6, 1, 4, 3, 2, 5, 0)
    return val


def type1b(val, param, swap):
    val = bitswap(val, *((1, 0, 3, 2, 5, 6, 4, 7), (2, 0, 5, 1, 7, 4, 6, 3),
                         (6, 4, 7, 2, 0, 5, 1, 3), (7, 1, 3, 6, 0, 2, 5, 4))[swap])
    if bit(val, 2) and bit(val, 0):
        val ^= bits(7, 4)
    if bit(val, 7):
        val ^= bits(2)
    if bit(val, 5):
        val ^= bits(7, 2)
    if bit(val, 1):
        val ^= bits(5)
    if bit(val, 6):
        val ^= bits(1)
    if bit(val, 4):
        val ^= bits(6, 5)
    if bit(val, 0):
        val ^= bits(6, 2, 1)
    if bit(val, 3):
        val ^= bits(7, 6, 2, 1, 0)
    val ^= bits(6, 4, 0)
    if bit(param, 3):
        val ^= bits(4, 1)
    if bit(param, 2):
        val ^= bits(7, 6, 3, 0)
    if bit(param, 1):
        val ^= bits(4, 3)
    if bit(param, 0):
        val ^= bits(6, 2, 1, 0)
    return val


def type2a(val, param, swap):
    val = bitswap(val, *((0, 1, 4, 3, 5, 6, 2, 7), (6, 3, 0, 5, 7, 4, 1, 2),
                         (1, 6, 4, 5, 0, 3, 7, 2), (4, 6, 7, 5, 2, 3, 1, 0))[swap])
    if bit(val, 3) or (bit(param, 1) and bit(val, 2)):
        val = bitswap(val, 6, 0, 7, 4, 3, 2, 1, 5)
    if bit(val, 5):
        val ^= bits(7)
    if bit(val, 6):
        val ^= bits(5)
    if bit(val, 0):
        val ^= bits(6)
    if bit(val, 4):
        val ^= bits(3, 0)
    if bit(val, 1):
        val ^= bits(2)
    val ^= bits(7, 6, 5, 4, 1)
    if bit(param, 2):
        val ^= bits(4, 3, 2, 1, 0)
    if bit(param, 3):
        if bit(param, 0):
            val = bitswap(val, 7, 6, 5, 3, 4, 1, 2, 0)
        else:
            val = bitswap(val, 7, 6, 5, 1, 2, 4, 3, 0)
    elif bit(param, 0):
        val = bitswap(val, 7, 6, 5, 2, 1, 3, 4, 0)
    return val


def type2b(val, param, swap):
    val = bitswap(val, *((1, 3, 4, 6, 5, 7, 0, 2), (0, 1, 5, 4, 7, 3, 2, 6),
                         (3, 5, 4, 1, 6, 2, 0, 7), (5, 2, 3, 0, 4, 7, 6, 1))[swap])
    if bit(val, 7) and bit(val, 3):
        val ^= bits(6, 4, 0)
    if bit(val, 7):
        val ^= bits(2)
    if bit(val, 5):
        val ^= bits(7, 3)
    if bit(val, 1):
        val ^= bits(5)
    if bit(val, 4):
        val ^= bits(7, 5, 3, 1)
    if bit(val, 7) and bit(val, 5):
        val ^= bits(4, 0)
    if bit(val, 5) and bit(val, 1):
        val ^= bits(4, 0)
    if bit(val, 6):
        val ^= bits(7, 5)
    if bit(val, 3):
        val ^= bits(7, 6, 5, 1)
    if bit(val, 2):
        val ^= bits(3, 1)
    val ^= bits(7, 3, 2, 1)
    if bit(param, 3):
        val ^= bits(6, 3, 1)
    if bit(param, 2):
        val ^= bits(7, 6, 5, 3, 2, 1)
    if bit(param, 1):
        val ^= bits(7)
    if bit(param, 0):
        val ^= bits(5, 2)
    return val


def type3a(val, param, swap):
    val = bitswap(val, *((5, 3, 1, 7, 0, 2, 6, 4), (3, 1, 2, 5, 4, 7, 0, 6),
                         (5, 6, 1, 2, 7, 0, 4, 3), (5, 6, 7, 0, 4, 2, 1, 3))[swap])
    if bit(val, 2):
        val ^= bits(7, 5, 4)
    if bit(val, 3):
        val ^= bits(0)
    if bit(param, 0):
        val = bitswap(val, 7, 2, 5, 4, 3, 1, 0, 6)
    if bit(val, 1):
        val ^= bits(6, 0)
    if bit(val, 3):
        val ^= bits(4, 2, 1)
    if bit(param, 3):
        val ^= bits(4, 3)
    if bit(val, 3):
        val = bitswap(val, 5, 6, 7, 4, 3, 2, 1, 0)
    if bit(val, 5):
        val ^= bits(2, 1)
    val ^= bits(6, 5, 4, 3)
    if bit(param, 2):
        val ^= bits(7)
    if bit(param, 1):
        val ^= bits(4)
    if bit(param, 0):
        val ^= bits(0)
    return val


def type3b(val, param, swap):
    val = bitswap(val, *((3, 7, 5, 4, 0, 6, 2, 1), (7, 5, 4, 6, 1, 2, 0, 3),
                         (7, 4, 3, 0, 5, 1, 6, 2), (2, 6, 4, 1, 3, 7, 0, 5))[swap])
    if bit(val, 2):
        val ^= bits(7)
    if bit(val, 7):
        val = bitswap(val, 7, 6, 3, 4, 5, 2, 1, 0)
    if bit(param, 3):
        val ^= bits(7)
    if bit(val, 4):
        val ^= bits(6)
    if bit(val, 1):
        val ^= bits(6, 4, 2)
    if bit(val, 7) and bit(val, 6):
        val ^= bits(1)
    if bit(val, 7):
        val ^= bits(1)
    if bit(param, 3):
        val ^= bits(7)
    if bit(param, 2):
        val ^= bits(0)
    if bit(param, 3):
        val = bitswap(val, 4, 6, 3, 2, 5, 0, 1, 7)
    if bit(val, 4):
        val ^= bits(1)
    if bit(val, 5):
        val ^= bits(4)
    if bit(val, 7):
        val ^= bits(2)
    val ^= bits(5, 3, 2)
    if bit(param, 1):
        val ^= bits(7)
    if bit(param, 0):
        val ^= bits(3)
    return val


TYPES = (type0, type0, type1a, type1b, type2a, type2b, type3a, type3b)


def decrypt_internal(val, key, opcode):
    key ^= 0xff
    if key == 0:
        return val
    k = [bit(key, n) for n in range(8)]
    typ = (k[0] ^ k[2]) | (k[0] ^ k[1] ^ k[2] ^ k[4]) << 1 | (k[4] ^ k[5]) << 2
    swap = (k[0] ^ k[1]) | (k[2] ^ k[3]) << 1
    param = (k[0] | (k[0] ^ k[2] ^ k[3]) << 1 | (k[0] ^ k[1] ^ k[6]) << 2 |
             (k[1] ^ k[6] ^ k[7]) << 3)
    if not opcode:
        param ^= 1
        typ ^= 1
    return TYPES[typ](val, param, swap)


def decrypt(key, addr, val, opcode):
    tbl = bitswap(addr, 15, 14, 13, 12, 11, 10, 8, 6, 4, 2, 1, 0)
    return decrypt_internal(val, key[tbl | (0 if opcode else 0x1000)], opcode)


# MAME mc8123_device::decode() over romconv's main image: 0000-7fff fixed,
# then 16 KB banks that all run at 8000-bfff. Returns (data, opcodes).
def decode(rom, key):
    data = bytearray(len(rom))
    ops = bytearray(len(rom))
    for i, src in enumerate(rom):
        adr = i if i < 0x8000 else (i & 0x3fff) | 0x8000
        ops[i] = decrypt(key, adr, src, True)
        data[i] = decrypt(key, adr, src, False)
    return bytes(data), bytes(ops)
