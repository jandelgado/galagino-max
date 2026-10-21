"""
ROM converter for Tutankham (Konami/Stern 1982)
Generates header files for GALAGONE ESP32-S3 emulator.

Hardware:
  Main CPU: MC6809E @ 1.5 MHz
  Sound: timeplt_audio (Z80 @ 1.789 MHz + 2x AY-3-8910)
  Video: 256x256 bitmap, 4bpp (2 pixels/byte), ROT90
  Palette: 16 colors, CPU-written, RRRGGGBB format

MAME memory map (Main CPU):
  0x0000-0x7FFF: Video RAM (32KB bitmap, 2 pixels/byte)
  0x8000-0x800F: Palette RAM (mirror 0xF0)
  0x8100:        Scroll register (mirror 0xF)
  0x8160:        DSW2 read (mirror 0xF)
  0x8180:        IN0 read (coin/start, mirror 0xF)
  0x81A0:        IN1 read (P1 controls, mirror 0xF)
  0x81C0:        IN2 read (P2 controls, mirror 0xF)
  0x81E0:        DSW1 read (mirror 0xF)
  0x8200-0x8207: LS259 latch (mirror 0xF8)
  0x8300:        Bank select (mirror 0xFF)
  0x8600:        Sound trigger (mirror 0xFF)
  0x8700:        Sound data latch (mirror 0xFF)
  0x8800-0x8FFF: Work RAM (2KB)
  0x9000-0x9FFF: Banked ROM (4KB, 16 banks selectable)
  0xA000-0xFFFF: Fixed ROM (24KB: m1+m2+3j+m4+m5+j6)

ROM files from tutankhm.zip:
  Fixed ROM: m1.1h (0xA000), m2.2h (0xB000), 3j.3h (0xC000),
             m4.4h (0xD000), m5.5h (0xE000), j6.6h (0xF000)
  Banked ROM: c1.1i - c9.9i (4KB each, banks 0-8)
  Sound ROM: s1.7a (0x0000), s2.8a (0x1000)
"""
from romconv.core.context import Context
from romconv.core.job import Job
from romconv.emit.asset import emit_compressed


def _load(ctx, filename):
    """Load a ROM file of the zip."""
    data = ctx.rom(filename)
    ctx.log.info(f"  Loaded {filename}: {len(data)} bytes")
    return data


def _write_header(ctx, filename, varname, data, comment=""):
    """Write data to a C header file."""
    with ctx.header(filename) as f:
        f.write(f"// {comment}\n")
        emit_compressed(f, varname, "unsigned char", "", len(data), list(data))
    ctx.log.info(f"  Written {filename} ({len(data)} bytes)")


def _convert(ctx: Context) -> None:
    # ══════════════════════════════════════════════════════════════════
    # 1. FIXED ROM — 24KB covering 0xA000-0xFFFF
    # ══════════════════════════════════════════════════════════════════
    ctx.log.info("=== Fixed CPU ROM (0xA000-0xFFFF) ===")

    # Load all fixed ROM files
    rom_m1 = _load(ctx, "m1.1h")   # 4KB at 0xA000
    rom_m2 = _load(ctx, "m2.2h")   # 4KB at 0xB000
    rom_3j = _load(ctx, "3j.3h")   # 4KB at 0xC000
    rom_m4 = _load(ctx, "m4.4h")   # 4KB at 0xD000
    rom_m5 = _load(ctx, "m5.5h")   # 4KB at 0xE000
    rom_j6 = _load(ctx, "j6.6h")   # 4KB at 0xF000 (interrupt vectors!)

    # Build 24KB ROM: 0xA000-0xFFFF
    cpu_rom = rom_m1 + rom_m2 + rom_3j + rom_m4 + rom_m5 + rom_j6
    expected_size = 6 * 4096  # 6 files * 4KB = 24KB
    assert len(cpu_rom) == expected_size, f"Expected {expected_size} bytes, got {len(cpu_rom)}"
    ctx.log.info(f"  CPU ROM total: {len(cpu_rom)} bytes (24KB: m1+m2+3j+m4+m5+j6)")

    # Verify interrupt vectors (at end of j6.6h = offset 0x5FFx)
    reset_vec = (cpu_rom[0x5FFE] << 8) | cpu_rom[0x5FFF]
    nmi_vec = (cpu_rom[0x5FFC] << 8) | cpu_rom[0x5FFD]
    irq_vec = (cpu_rom[0x5FF8] << 8) | cpu_rom[0x5FF9]
    ctx.log.info(f"  Reset vector: 0x{reset_vec:04X}")
    ctx.log.info(f"  NMI vector:   0x{nmi_vec:04X}")
    ctx.log.info(f"  IRQ vector:   0x{irq_vec:04X}")

    _write_header(ctx, "tutankhm_rom.h", "tutankhm_rom", cpu_rom,
                  "Tutankham fixed CPU ROM (24KB: 0xA000-0xFFFF, indexed as rom[addr - 0xA000])")

    # ══════════════════════════════════════════════════════════════════
    # 2. BANKED ROM — c1-c9 (9 x 4KB = 36KB, banks at 0x9000-0x9FFF)
    # ══════════════════════════════════════════════════════════════════
    ctx.log.info("=== Banked ROM (0x9000-0x9FFF, 4KB per bank) ===")

    bank_roms = b""
    for i in range(1, 10):
        bank_roms += _load(ctx, f"c{i}.{i}i")

    # Pad to 16 banks (16 x 4KB = 64KB) with zeros for unused banks 9-15
    bank_roms += b'\x00' * (16 * 0x1000 - len(bank_roms))
    ctx.log.info(f"  Bank ROM total: {len(bank_roms)} bytes (16 x 4KB, banks 9-15 empty)")

    _write_header(ctx, "tutankhm_bank_rom.h", "tutankhm_bank_rom", bank_roms,
                  "Tutankham banked ROM (64KB: 16 x 4KB banks at 0x9000-0x9FFF, "
                  "indexed as bank_rom[bank*0x1000 + (addr-0x9000)])")

    # ══════════════════════════════════════════════════════════════════
    # 3. SOUND CPU ROM — 8KB (timeplt_audio board)
    # ══════════════════════════════════════════════════════════════════
    ctx.log.info("=== Sound CPU ROM ===")

    snd_s1 = _load(ctx, "s1.7a")   # 4KB at 0x0000
    snd_s2 = _load(ctx, "s2.8a")   # 4KB at 0x1000

    snd_rom = snd_s1 + snd_s2
    ctx.log.info(f"  Sound ROM total: {len(snd_rom)} bytes (8KB)")

    _write_header(ctx, "tutankhm_snd_rom.h", "tutankhm_snd_rom", snd_rom,
                  "Tutankham sound CPU ROM (8KB: s1+s2, timeplt_audio Z80)")

    ctx.log.info("--- OPERAZIONE COMPLETATA ---")


JOB = Job(name="tutankhm", convert=_convert, machine="tutankhm", zips=("tutankhm.zip",))
