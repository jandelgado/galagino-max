#!/usr/bin/env python3
import sys
sys.path.insert(0, "internal/pyconv")
from romdata_emit import emit_compressed

def parse_palette(name, name2=None):
    # galaga: the palette contains 32 8 bit rgb values. The first 16 are
    # used by sprites and the second 16 by tiles

    # if the name is a comma seperated list, the there are three seperate
    # files for r, g and b
    if len(name.split(",")) == 3:
        palettes = []
        for fname in name.split(","):
            f = open(fname, "rb")
            palettes.append(f.read())
            f.close()

        # convert into palette_data (array of tuples)
        palette_data = []
        for i in range(256):
            palette_data.append((palettes[0][i],palettes[1][i],palettes[2][i]))
    else:
        f = open(name, "rb")
        palette_data = f.read()
        f.close()

    if name2:
        f = open(name2, "rb")
        palette_data_2 = f.read()
        f.close()
    else:
        palette_data_2 = None

    # map to 16 bit rgb in swapped format for ili9341 display
    palette = []
    for idx in range(len(palette_data)):
        c = palette_data[idx]
        
        # palette_data may be an array of three files
        if not isinstance(c, int) and len(c) == 3:
            # the proms contain 4 bit data
            # convert to rgb 565 (5 bit red, 6 bit green, 5 bit blue)
            r = c[0] << 1
            g = c[1] << 2
            b = c[2] << 1
        else:

            # dkong uses two 4 bit color proms, so there's a second one
            if palette_data_2:
                c = (palette_data_2[idx] << 4) + c 

                # donkey kong rrrgggbb mapping
                r = 31 - 31*((c>>5) & 0x7)//7
                g = 63 - 63*((c>>2) & 0x7)//7
                b = 31 - 31*((c>>0) & 0x3)//3
            else:
                # galaga, pacman and frogger bbgggrrr mapping
            
                # This doesn't 100% match the weighting each bit has
                # on the real machine with 1000, 470 and 220 ohms resistors.
                #
                # Here the weighting is 4/7, 2/7 and 1/7 while on the
                # real thing it's 4.15/7, 1.94/7 and 0.91/7. So the
                # weighting of the lsb is slightly higher on the real
                # device.
                
                b = 31*((c>>6) & 0x3)//3
                g = 63*((c>>3) & 0x7)//7
                r = 31*((c>>0) & 0x7)//7

        rgb = (r << 11) + (g << 5) + b
        rgbs = ((rgb & 0xff00) >> 8) + ((rgb & 0xff) << 8)
        #print(rgbs, r, g, b)
        palette.append(rgbs)

    return palette

def parse_colormap_1942_tiles(id, inname, palette, outname):
    inname, mapname0, mapname1 = inname.split(",")
    f = open(inname, "rb")
    colormap_data = f.read()
    f.close()

    # load map parts
    f = open(mapname0, "rb");  map0 = f.read(); f.close()
    f = open(mapname1, "rb");  map1 = f.read(); f.close()

    map = []
    for i in range(len(map0)):
        # only use lower 2 bits of sb-2.d1
        map.append(map0[i] + 16*(map1[i]&3))

    # output of colormap_data is 4 bit and goes into lower four
    # bits of sb-3/sb-2 map. The upper 4 bits come externally,
    # resulting in 16 tables with 32 x 8 color maps each
    f = open(outname, "w")

    flat = []
    for tab in range(16):
        for idx in range(32):
            c = colormap_data[8*idx:8*(idx+1)]
            # check if values are sane
            if ( c[0] < 0 or c[0] > 15 or c[0] < 0 or c[1] > 15 or
                 c[2] < 0 or c[2] > 15 or c[3] < 0 or c[3] > 15):
                raise ValueError("Color index out of range")
            for a in c:
                flat.append(palette[map[16*tab+a]])

    emit_compressed(f, id, "unsigned short", "[32][8]", 16, flat)
    f.close()

        
def parse_colormap(id, inname, palette, outname, compress=False):
    # inname may be three comma seperated files. These are 1942 tiles maps
    # with an additinal indirection
    if len(inname.split(",")) == 3:
        parse_colormap_1942_tiles(id, inname, palette, outname)
        return

    f = open(inname, "rb")
    colormap_data = f.read()
    f.close()

    # the colormaps contain 64*4 4-bit entries that point
    # to palette entries.
    if len(colormap_data) != 256:
        raise ValueError("Missing colormap data")

    # write as c source
    f = open(outname, "w")

    flat = []
    for idx in range(64):
        c = colormap_data[4*idx:4*(idx+1)]
        # check if values are sane
        if ( c[0] < 0 or c[0] > 15 or c[0] < 0 or c[1] > 15 or
             c[2] < 0 or c[2] > 15 or c[3] < 0 or c[3] > 15):
            raise ValueError("Color index out of range")
        for a in c:
            flat.append(palette[a])

    if compress:
        emit_compressed(f, id, "unsigned short", "[4]", 64, flat)
    else:
        colors = []
        for idx in range(64):
            colors.append("{" + ",".join([ hex(v) for v in flat[4*idx:4*idx+4] ]) +"}")
        print("const unsigned short "+id+"[][4] = {", file=f )
        print(",\n".join(colors), file=f)
        print("};", file=f)

    f.close()

def parse_colormap_dkong(id, inname, palette, outname, compress=False):
    f = open(inname, "rb")
    colormap_data = f.read()
    f.close()

    if len(colormap_data) != 256:
        raise ValueError("Missing colormap data")
    
    # write as c source
    f = open(outname, "w")

    if compress:
        flat = []
        for s in range(4):
            for idx in colormap_data:
                idx += 16*s
                for a in [4*idx+0, 4*idx+1, 4*idx+2, 4*idx+3]:
                    flat.append(palette[a])
        emit_compressed(f, id, "unsigned short", "[256][4]", 4, flat)
    else:
        print("const unsigned short "+id+"[][256][4] = {", file=f )
        screens = [ ]
        for s in range(4):
            colors = []
            for idx in colormap_data:
                idx += 16*s
                c = [4*idx+0, 4*idx+1, 4*idx+2, 4*idx+3]
                colors.append("{" + ",".join([ hex(palette[a]) for a in c ]) +"}")
            screens.append("{" + ",\n".join(colors)+"}")
        print(",\n".join(screens), file=f)
        print("};", file=f)

    print("", file=f)
    print("", file=f)

    # create the four sprite colormap tables (always plain: 512 bytes total,
    # too small for zlib to net a win)
    print("const unsigned short "+id+"_sprite[][16][4] = {", file=f )
    screens = [ ]
    for s in range(4):
        colors = []
        for i in range(16):
            idx = i + 16*s
            c = [4*idx+0, 4*idx+1, 4*idx+2, 4*idx+3]
            colors.append("{" + ",".join([ hex(palette[a]) for a in c ]) +"}")
        screens.append("{" + ",\n".join(colors)+"}")
    print(",\n".join(screens), file=f)
    print("};", file=f)
    
    f.close()

def dump_palette(id, outname, palette):
    # write as c source
    f = open(outname, "w")
    print("const unsigned short "+id+"[][4] = {", file=f )
    colors = []
    for idx in range(8):
        c = palette[4*idx:4*(idx+1)]
        colors.append("{" + ",".join([ hex(a) for a in c ]) +"}")
    
    print(",\n".join(colors), file=f)
    print("};", file=f)
    f.close()

args = sys.argv[1:]
compress = False
if args and args[0] == "-c":
    compress = True
    args.pop(0)

if len(args) < 3 or len(args) == 4 or len(args) > 6:
    print("Invalid arguments")
    exit(-1)

if len(args) == 3:
    palette = parse_palette(args[1])
    dump_palette(args[0], args[2], palette)
elif len(args) == 5:
    palette = parse_palette(args[1])
    offset = int(args[2])
    if offset >= 0:
        parse_colormap(args[0], args[3], palette[offset:offset+16], args[4], compress)
    else:
        parse_colormap(args[0], args[3], palette, args[4], compress)
else:
    # len 6
    # dkong has the palette in two 4 bit roms
    palette = parse_palette(args[1], args[2])
    parse_colormap_dkong(args[0], args[4], palette, args[5], compress)

