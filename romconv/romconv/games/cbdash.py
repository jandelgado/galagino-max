# Boulder Dash (Data East 1985, DECO Cassette, set "cbdash", BIOS B).
#
# The tape is not emulated. cbdash.cas is a list of 256-byte blocks; the
# BIOS drops byte 0 of each block (a repeat of byte 1) and copies bytes
# 1..255. Block 0 is a header, blocks 1..90 go to main RAM 0x0500, blocks
# 91..102 are sent to sound RAM 0x0200, block 103 is a trailer. Verified against a MAME tape boot.
#
# Music: the C64 tune, user supplied like the ROMs (romszip/Boulder_Dash.sid,
# HVSC MUSICIANS/L/Liepa_Peter/Boulder_Dash.sid), rebuilt into a compact PSID.
from types import MappingProxyType

from romconv.core.context import Context
from romconv.core.errors import ConversionError
from romconv.core.job import Job
from romconv.emit.write import write_bytes
from romconv.misc.userfile import read_user_file

_CAS = ("cbdash.cas", "5d163d8e31c58b20679c6be06b1aa02df621822b")
_BIOS = ("v0b-.7e", "063f83020ba3d6f43ab8471f95ca919767b93aa4")
_AUDIO = ("v1-.5a", "0097f38beb4872e735e560148052e258a26b08fd")

_SID_FILE = "Boulder_Dash.sid"
_SID_SHA1 = "2d795692e2c2ccd99dba58e85da487086923b329"

# Boulder_Dash.sid (PSID, NTSC, load $1000) is a game rip: init copies
# file pages all over C64 memory, play ($875B) only uses three of them:
#   file $1000-$10FF -> $86CA  freq table + play code, kept in place
#   file $1100-$11FF -> $5FE8  note pairs, moved to $87CA
#   file $1360-$136C -> $9800  play state ($9800, $9801, $980C), moved to $88CA
# SidPlayer maps one RAM window, so the rebuilt PSID loads these 531 bytes
# contiguously at $86CA, one octave up (see SID_OCTAVES_UP). Its init only sets the volume, as the original;
# the state's start values are part of the payload.
_SID_FILE_LOAD = 0x1000
_SID_LOAD = 0x86CA
_SID_PLAY = 0x875B
_SID_PLAY_END = 0x87C6  # RTS
_SID_MOVES = (  # (file address, C64 address, new address, length)
    (0x1100, 0x5FE8, 0x87CA, 0x100),
    (0x1360, 0x9800, 0x88CA, 0x0D),
)
_SID_INIT = 0x88D7
_SID_INIT_CODE = bytes([0xA9, 0x0F,         # LDA #$0F
                        0x8D, 0x18, 0xD4,   # STA $D418  volume
                        0x60])              # RTS
_SID_REGS = range(0xD400, 0xD419)
# One octave up, as the arcade's AY version: the triangle voices around
# 87-350 Hz are barely audible on a small speaker. Only the entries the
# notes use are frequencies (15..58), the table's start is other data.
_SID_FREQ_TABLE = _SID_LOAD
_SID_NOTES = 0x87CA
_SID_OCTAVES_UP = 1
# opcodes in the play routine -> length (abs operands: 3)
_OP_LEN = MappingProxyType({0x0A: 1, 0x18: 1, 0x60: 1, 0xA8: 1,
                            0x29: 2, 0x49: 2, 0x69: 2, 0xA9: 2, 0xC9: 2, 0xD0: 2,
                            0x8D: 3, 0xAD: 3, 0xAE: 3, 0xB9: 3, 0xBD: 3, 0xEE: 3})

_PSID_LOAD, _PSID_INIT, _PSID_PLAY, _PSID_DATA = 0x08, 0x0A, 0x0C, 0x7C


def _blocks(cas, first, last):
    return b"".join(cas[b * 256 + 1:b * 256 + 256] for b in range(first, last + 1))


def _relocate(addr):
    for _, old, new, n in _SID_MOVES:
        if old <= addr < old + n:
            return addr - old + new
    if _SID_LOAD <= addr < _SID_LOAD + 0x100 or addr in _SID_REGS:
        return addr
    raise ConversionError(f"Boulder_Dash.sid: play uses ${addr:04X}, outside the rebuilt tune")


def _sid_tune(ctx):
    data = read_user_file(ctx, _SID_FILE, _SID_SHA1, "HVSC Liepa_Peter/Boulder_Dash.sid")
    c64 = data[_PSID_DATA + 2:]  # load address 0: PRG style prefix

    def page(file_addr, n):
        return bytearray(c64[file_addr - _SID_FILE_LOAD:file_addr - _SID_FILE_LOAD + n])

    tune = page(_SID_FILE_LOAD, 0x100)
    for file_addr, _, new, n in _SID_MOVES:
        assert _SID_LOAD + len(tune) == new
        tune += page(file_addr, n)
    assert _SID_LOAD + len(tune) == _SID_INIT
    tune += _SID_INIT_CODE

    notes = tune[_SID_NOTES - _SID_LOAD:_SID_NOTES - _SID_LOAD + 0x100]
    for note in set(notes):
        i = _SID_FREQ_TABLE - _SID_LOAD + 2 * note
        freq = (tune[i] | tune[i + 1] << 8) << _SID_OCTAVES_UP
        assert freq <= 0xFFFF
        tune[i], tune[i + 1] = freq & 0xFF, freq >> 8

    pc = _SID_PLAY
    while pc <= _SID_PLAY_END:
        i = pc - _SID_LOAD
        op = tune[i]
        if op not in _OP_LEN:
            raise ConversionError(f"Boulder_Dash.sid: unexpected opcode {op:02X} at ${pc:04X}")
        if _OP_LEN[op] == 3:
            addr = _relocate(tune[i + 1] | tune[i + 2] << 8)
            tune[i + 1], tune[i + 2] = addr & 0xFF, addr >> 8
        pc += _OP_LEN[op]

    header = bytearray(data[:_PSID_DATA])  # keeps names and flags (NTSC)
    for ofs, v in ((_PSID_LOAD, _SID_LOAD), (_PSID_INIT, _SID_INIT), (_PSID_PLAY, _SID_PLAY)):
        header[ofs:ofs + 2] = v.to_bytes(2, "big")
    return bytes(header + tune)


def _convert(ctx: Context) -> None:
    cas = ctx.rom(*_CAS, zip="cbdash.zip")
    bios = ctx.rom(*_BIOS, zip="decocass.zip")
    audio = ctx.rom(*_AUDIO, zip="decocass.zip")

    main_img = _blocks(cas, 1, 90)
    sound_img = _blocks(cas, 91, 102)
    assert len(main_img) == 22950 and len(sound_img) == 3060
    # block 103 is an "EOB001" trailer the BIOS never reads
    assert cas[103 * 256 + 1:103 * 256 + 4] == b"EOB" and not any(cas[104 * 256:])

    total = write_bytes(ctx, "cbdash_rom_main.h", "cbdash_rom_main", main_img,
                        "main RAM image, load at 0x0500, entry 0x0503")
    total += write_bytes(ctx, "cbdash_rom_sound.h", "cbdash_rom_sound", sound_img,
                         "sound RAM image, load at 0x0200, entry 0x0203")
    total += write_bytes(ctx, "cbdash_rom_bios.h", "cbdash_rom_bios", bios,
                         "BIOS B, main CPU 0xf000-0xffff, encrypted opcodes")
    total += write_bytes(ctx, "cbdash_rom_audio.h", "cbdash_rom_audio", audio,
                         "sound CPU ROM 0xf800-0xffff")

    sid = _sid_tune(ctx)
    write_bytes(ctx, "cbdash_sid.h", "cbdash_sid", sid,
                "C64 Boulder Dash PSID (Peter Liepa, 1984 First Star), rebuilt contiguous")
    total += 2 * len(sid) - _PSID_DATA  # the file + the SidPlayer RAM window
    ctx.log.info(f"Arena (decompressed assets): {total} bytes")


JOB = Job(name="cbdash", convert=_convert, machine="cbdash", zips=("cbdash.zip", "decocass.zip"))
