#!/usr/bin/env python3
import sys
sys.path.insert(0, "internal/pyconv")

from asset_emit import emit_compressed

PATCHES = {
    "galaga_rom_cpu1":
    [
        # jump over tile ram test
        ( 0x3382, 0x06, 0xc3 ),     # 06, jp
        ( 0x3383, 0x0a, 0x35 ),     # xx35
	( 0x3384, 0xd9, 0x34 ),     # 34xx
	# only one ramtest round, instead of 30
	( 0x348a, 0x1e, 0x01 ),
        # skip rom test
	( 0x352b, 0xe5, 0xc9 )      # ret
    ]
}

def bit_permute_step(x, m, shift):
    t = ((x >> shift) ^ x) & m
    x = (x ^ t) ^ (t << shift)
    return x

def parse_rom(id, infiles, outfile, apply_patches = False, decode = False, compress = False):
    offset = 0
    all_bytes = bytearray()

    for name_idx in range(len(infiles)):
        f = open(infiles[name_idx], "rb")
        rom_data = f.read()
        f.close()

        # the first frogger audio cpu rom has bits
        # d0 and d1 swapped. Fix this
        if rom_data[:8] == bytes([0x05,0x00,0x22,0x00,0x40,0xc3,0x0b,0x02]):
            rom_data = list(rom_data)
            for i in range(len(rom_data)):
                rom_data[i] = (rom_data[i] & 0xfc) | ((rom_data[i] & 2)>>1) | ((rom_data[i] & 1)<<1)
            rom_data = bytes(rom_data)

        # apply patches
        if apply_patches:
            rom_data = list(rom_data)
            for i in PATCHES:
                if i == id:
                    for p in PATCHES[i]:
                        if p[0] - offset < len(rom_data):
                            if rom_data[p[0] - offset] == p[1]:
                                print("Patching", hex(p[0]), ":", p[1], "->", p[2])
                                rom_data[p[0] - offset] = p[2]
                            else:
                                raise ValueError("Unexpected patchdata")
            rom_data = bytes(rom_data)

        offset += len(rom_data)

        if decode:
            rom_data = list(rom_data)
            for i in range(len(rom_data)):
                rom_data[i] = bit_permute_step(rom_data[i], 8, 2)
            rom_data = bytes(rom_data)

        all_bytes.extend(rom_data)

    of = open(outfile, "w")
    if compress:
        emit_compressed(of, id, "unsigned char", "", len(all_bytes), list(all_bytes))
    else:
        print("const unsigned char "+id+"[] = {\n  ", end="", file=of)
        hexs = ["0x{:02X}".format(b) for b in all_bytes]
        for i, h in enumerate(hexs):
            print(h, end="", file=of)
            if i != len(hexs) - 1:
                print(",", end="", file=of)
                if i & 15 == 15:
                    print("\n  ", end="", file=of)
        print("", file=of)
        print("};", file=of)
    of.close()

FLAGS = {"-p": "apply_patches", "-d": "decode", "-c": "compress"}

args = sys.argv[1:]
opts = {"apply_patches": False, "decode": False, "compress": False}
while args and args[0] in FLAGS:
    opts[FLAGS[args.pop(0)]] = True

if len(args) < 3:
    print("Invalid arguments")
    exit(-1)

parse_rom(args[0], args[1:-1], args[-1], **opts)
