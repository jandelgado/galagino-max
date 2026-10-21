"""
Convertitore ROM Ms. Pac-Man con applicazione patch lineare
- Genera il file spritemap temporaneo (come il convertitore originale)
- Applica le patch linearmente valore per valore per posizione
- Produce il file finale corretto
- Elimina i file temporanei
"""
from pathlib import Path

from romconv.core.context import Context
from romconv.core.job import Job
from romconv.emit.asset import emit_compressed
from romconv.gfx.patchtable import apply_patch_values, read_patch_table

_HERE = Path(__file__).parent


def _decode_byte(b):
    return (((b>>0)&1)<<7 | ((b>>4)&1)<<6 | ((b>>5)&1)<<5 |
            ((b>>7)&1)<<4 | ((b>>6)&1)<<3 | ((b>>3)&1)<<2 |
            ((b>>2)&1)<<1 | ((b>>1)&1)<<0)

def _addr_swap11(i):
    return (((i>>8)&1)<<10 | ((i>>7)&1)<<9  | ((i>>5)&1)<<8  |
            ((i>>9)&1)<<7  | ((i>>10)&1)<<6 | ((i>>6)&1)<<5  |
            ((i>>3)&1)<<4  | ((i>>4)&1)<<3  | ((i>>2)&1)<<2  |
            ((i>>1)&1)<<1  | ((i>>0)&1)<<0)

def _addr_swap12(i):
    return (((i>>11)&1)<<11 | ((i>>3)&1)<<10 | ((i>>7)&1)<<9  |
            ((i>>9)&1)<<8   | ((i>>10)&1)<<7 | ((i>>8)&1)<<6  |
            ((i>>6)&1)<<5   | ((i>>5)&1)<<4  | ((i>>4)&1)<<3  |
            ((i>>2)&1)<<2   | ((i>>1)&1)<<1  | ((i>>0)&1)<<0)


def _generate_sprite_values(gfx5e, gfx5f):
    sprite_offset = 2048
    max_gfx_size = len(gfx5f)
    SPRITE_COUNT = 64
    
    # Unflipped: pacman::blit_sprite flips at draw time.
    all_values = []
    
    for s in range(SPRITE_COUNT):
        for r in range(16):
            base = sprite_offset + (s * 64) + r
            
            b0l = gfx5f[base] if base < max_gfx_size else 0
            b0h = gfx5f[base + 16] if (base + 16) < max_gfx_size else 0
            b1l = gfx5f[base + 32] if (base + 32) < max_gfx_size else 0
            b1h = gfx5f[base + 48] if (base + 48) < max_gfx_size else 0
            
            wl, wh = 0, 0
            for bit in range(8):
                p0l, p1l = (b0l >> (7-bit)) & 1, (b1l >> (7-bit)) & 1
                p0h, p1h = (b0h >> (7-bit)) & 1, (b1h >> (7-bit)) & 1
                wl |= (((p1l << 1) | p0l) << (bit * 2))
                wh |= (((p1h << 1) | p0h) << (bit * 2))
            val = (wh << 16) | wl
            
            all_values.append(val)

    return all_values

def _convert(ctx: Context) -> None:
    # Carica i ROM
    pac6e = ctx.rom("pacman.6e")
    pac6f = ctx.rom("pacman.6f")
    pac6h = ctx.rom("pacman.6h")
    u5 = ctx.rom("u5")
    u6 = ctx.rom("u6")
    u7 = ctx.rom("u7")
    gfx5e = ctx.rom("5e")
    gfx5f = ctx.rom("5f")

    # 1. CPU ROM
    ctx.log.info("Generazione CPU ROM...")
    drom = bytearray([0xFF] * 0xC000)
    for i in range(0x1000):
        drom[0x0000 + i] = pac6e[i]
        drom[0x1000 + i] = pac6f[i]
        drom[0x2000 + i] = pac6h[i]
    for i in range(0x1000):
        drom[0x3000 + i] = _decode_byte(u7[_addr_swap12(i) & 0xFFF])
    for i in range(0x800):
        drom[0x8000 + i] = _decode_byte(u5[_addr_swap11(i) & 0x7FF])
        drom[0x8800 + i] = _decode_byte(u6[(_addr_swap12(i) & 0x7FF) + 0x800])
        drom[0x9000 + i] = _decode_byte(u6[_addr_swap12(i) & 0x7FF])
        drom[0x9800 + i] = pac6f[0x800 + i]

    # Patch CPU (originali)
    patches = [(0x0410, 0x8008), (0x08E0, 0x81D8), (0x0A30, 0x8118), (0x0BD0, 0x80D8),
               (0x0C20, 0x8120), (0x0E58, 0x8168), (0x0EA8, 0x8198), (0x1000, 0x8020),
               (0x1008, 0x8010), (0x1288, 0x8098), (0x1348, 0x8048), (0x1688, 0x8088),
               (0x16B0, 0x8188), (0x16D8, 0x80C8), (0x16F8, 0x81C8), (0x19A8, 0x80A8),
               (0x19B8, 0x81A8), (0x2060, 0x8148), (0x2108, 0x8018), (0x21A0, 0x81A0),
               (0x2298, 0x80A0), (0x23E0, 0x80E8), (0x2418, 0x8000), (0x2448, 0x8058),
               (0x2470, 0x8140), (0x2488, 0x8080), (0x24B0, 0x8180), (0x24D8, 0x80C0),
               (0x24F8, 0x81C0), (0x2748, 0x8050), (0x2780, 0x8090), (0x27B8, 0x8190),
               (0x2800, 0x8028), (0x2B20, 0x8100), (0x2B30, 0x8110), (0x2BF0, 0x81D0),
               (0x2CC0, 0x80D0), (0x2CD8, 0x80E0), (0x2CF0, 0x81E0), (0x2D60, 0x8160)]
    for dst, src in patches:
        for i in range(8):
            drom[dst + i] = drom[src + i]

    # 2. TILEMAP
    ctx.log.info("Generazione mspacman_tilemap.h...")
    tile_count = 256
    tile_base = []
    
    for t in range(tile_count):
        for row in range(8):
            idx = t * 8 + row
            b0, b1 = gfx5e[idx], gfx5f[idx]
            val = 0
            for bit in range(8):
                p0, p1 = (b0 >> (7-bit)) & 1, (b1 >> (7-bit)) & 1
                val |= (((p1 << 1) | p0) << (bit * 2))
            tile_base.append(val)
    
    tile_base = apply_patch_values(tile_base, read_patch_table(_HERE / "til_patch.table"), 0xFFFF)

    with ctx.header("mspacman_tilemap.h") as f:
        emit_compressed(f, "mspacman_tilemap", "unsigned short", "[8]", tile_count, tile_base)
    ctx.log.info("  Scritto mspacman_tilemap.h")

    # 3. SPRITEMAP - genera i valori base, applica la patch per posizione
    # direttamente sulla lista piatta (non piu' a regex su testo C, ora che
    # l'array e' compresso), poi scrive il file finale.
    ctx.log.info("=== GENERAZIONE SPRITEMAP ===")

    sprite_values = _generate_sprite_values(gfx5e, gfx5f)
    ctx.log.info(f"  Generati {len(sprite_values)} valori")

    sprite_values = apply_patch_values(sprite_values, read_patch_table(_HERE / "spr_patch.table"), 0xFFFFFFFF)

    with ctx.header("mspacman_spritemap.h") as f:
        emit_compressed(f, "mspacman_sprites", "uint32_t", "[16]", 64, sprite_values)

    ctx.log.info("  File finale: mspacman_spritemap.h")

    # 4. ROM FILES
    ctx.log.info("Generazione ROM files...")
    with ctx.header("mspacman_pacrom.h") as f:
        emit_compressed(f, "mspacman_pacrom", "unsigned char", "", 0x4000, list(drom[0:0x4000]))

    with ctx.header("mspacman_auxrom.h") as f:
        emit_compressed(f, "mspacman_auxrom", "unsigned char", "", 0x2000, list(drom[0x8000:0xA000]))

    ctx.log.info("Completato con successo.")


JOB = Job(name="mspacman", convert=_convert, machine="mspacman", zips=("mspacman.zip",), deps=("pacman",))
