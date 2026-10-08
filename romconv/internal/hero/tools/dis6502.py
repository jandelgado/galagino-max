#!/usr/bin/env python3
"""Recursive-descent 6502 disassembler for Atari 2600 F8 (2 x 4K) carts.

usage: dis6502.py rom.a26 [extra_entries.txt] [labels.txt] > out.asm
extra_entries: lines "bank addr" (hex) of additional code entry points.
labels: lines "addr name" (hex) for RAM/TIA symbols and code labels.
"""
import sys

MODES = {
    'imp': 1, 'acc': 1, 'imm': 2, 'zp': 2, 'zpx': 2, 'zpy': 2, 'izx': 2,
    'izy': 2, 'rel': 2, 'abs': 3, 'abx': 3, 'aby': 3, 'ind': 3,
}

OPS = {}
def op(code, name, mode):
    OPS[code] = (name, mode)

for n, base in (('ora', 0x00), ('and', 0x20), ('eor', 0x40), ('adc', 0x60),
                ('sta', 0x80), ('lda', 0xa0), ('cmp', 0xc0), ('sbc', 0xe0)):
    for off, m in ((1, 'izx'), (5, 'zp'), (9, 'imm'), (0xd, 'abs'),
                   (0x11, 'izy'), (0x15, 'zpx'), (0x19, 'aby'), (0x1d, 'abx')):
        if n == 'sta' and m == 'imm':
            continue
        op(base + off, n, m)
for n, base in (('asl', 0x00), ('rol', 0x20), ('lsr', 0x40), ('ror', 0x60)):
    op(base + 6, n, 'zp'); op(base + 0xa, n, 'acc'); op(base + 0xe, n, 'abs')
    op(base + 0x16, n, 'zpx'); op(base + 0x1e, n, 'abx')
for n, base in (('dec', 0xc0), ('inc', 0xe0)):
    op(base + 6, n, 'zp'); op(base + 0xe, n, 'abs')
    op(base + 0x16, n, 'zpx'); op(base + 0x1e, n, 'abx')
op(0x86, 'stx', 'zp'); op(0x8e, 'stx', 'abs'); op(0x96, 'stx', 'zpy')
op(0x84, 'sty', 'zp'); op(0x8c, 'sty', 'abs'); op(0x94, 'sty', 'zpx')
op(0xa2, 'ldx', 'imm'); op(0xa6, 'ldx', 'zp'); op(0xae, 'ldx', 'abs')
op(0xb6, 'ldx', 'zpy'); op(0xbe, 'ldx', 'aby')
op(0xa0, 'ldy', 'imm'); op(0xa4, 'ldy', 'zp'); op(0xac, 'ldy', 'abs')
op(0xb4, 'ldy', 'zpx'); op(0xbc, 'ldy', 'abx')
op(0xe0, 'cpx', 'imm'); op(0xe4, 'cpx', 'zp'); op(0xec, 'cpx', 'abs')
op(0xc0, 'cpy', 'imm'); op(0xc4, 'cpy', 'zp'); op(0xcc, 'cpy', 'abs')
op(0x24, 'bit', 'zp'); op(0x2c, 'bit', 'abs')
for code, n in ((0x10, 'bpl'), (0x30, 'bmi'), (0x50, 'bvc'), (0x70, 'bvs'),
                (0x90, 'bcc'), (0xb0, 'bcs'), (0xd0, 'bne'), (0xf0, 'beq')):
    op(code, n, 'rel')
op(0x4c, 'jmp', 'abs'); op(0x6c, 'jmp', 'ind'); op(0x20, 'jsr', 'abs')
for code, n in ((0x00, 'brk'), (0x40, 'rti'), (0x60, 'rts'), (0x08, 'php'),
                (0x28, 'plp'), (0x48, 'pha'), (0x68, 'pla'), (0x88, 'dey'),
                (0xa8, 'tay'), (0xc8, 'iny'), (0xe8, 'inx'), (0x18, 'clc'),
                (0x38, 'sec'), (0x58, 'cli'), (0x78, 'sei'), (0x98, 'tya'),
                (0xb8, 'clv'), (0xd8, 'cld'), (0xf8, 'sed'), (0x8a, 'txa'),
                (0x9a, 'txs'), (0xaa, 'tax'), (0xba, 'tsx'), (0xca, 'dex'),
                (0xea, 'nop')):
    op(code, n, 'imp')

TIA_W = ['VSYNC', 'VBLANK', 'WSYNC', 'RSYNC', 'NUSIZ0', 'NUSIZ1', 'COLUP0',
         'COLUP1', 'COLUPF', 'COLUBK', 'CTRLPF', 'REFP0', 'REFP1', 'PF0', 'PF1',
         'PF2', 'RESP0', 'RESP1', 'RESM0', 'RESM1', 'RESBL', 'AUDC0', 'AUDC1',
         'AUDF0', 'AUDF1', 'AUDV0', 'AUDV1', 'GRP0', 'GRP1', 'ENAM0', 'ENAM1',
         'ENABL', 'HMP0', 'HMP1', 'HMM0', 'HMM1', 'HMBL', 'VDELP0', 'VDELP1',
         'VDELBL', 'RESMP0', 'RESMP1', 'HMOVE', 'HMCLR', 'CXCLR']
TIA_R = ['CXM0P', 'CXM1P', 'CXP0FB', 'CXP1FB', 'CXM0FB', 'CXM1FB', 'CXBLPF',
         'CXPPMM', 'INPT0', 'INPT1', 'INPT2', 'INPT3', 'INPT4', 'INPT5']
RIOT = {0x280: 'SWCHA', 0x281: 'SWACNT', 0x282: 'SWCHB', 0x283: 'SWBCNT',
        0x284: 'INTIM', 0x285: 'TIMINT', 0x294: 'TIM1T', 0x295: 'TIM8T',
        0x296: 'TIM64T', 0x297: 'T1024T'}

WRITES = ('sta', 'stx', 'sty')
ORG = (0xd000, 0xf000)


class Rom:
    def __init__(self, data):
        assert len(data) == 8192
        self.data = data

    def byte(self, bank, addr):
        return self.data[bank * 4096 + (addr & 0xfff)]

    def word(self, bank, addr):
        return self.byte(bank, addr) | (self.byte(bank, addr + 1) << 8)


def is_rom(a):
    return a & 0x1000


def hotspot(a):
    """bank selected by an access to a, or None"""
    if is_rom(a) and (a & 0xfff) in (0xff8, 0xff9):
        return (a & 0xfff) - 0xff8
    return None


def target(rom, bank, pc):
    opc = rom.byte(bank, pc)
    name, mode = OPS[opc]
    n = MODES[mode]
    if n == 2:
        v = rom.byte(bank, pc + 1)
    elif n == 3:
        v = rom.word(bank, pc + 1)
    else:
        v = None
    return name, mode, n, v


def trace(rom, entries):
    """returns {(bank, addr): (name, mode, len, operand)} and xrefs"""
    code = {}
    xref = {}  # (bank, addr) -> set of (bank, from)
    todo = list(entries)
    while todo:
        bank, pc = todo.pop()
        pc = ORG[bank] | (pc & 0xfff)
        while (bank, pc) not in code:
            opc = rom.byte(bank, pc)
            if opc not in OPS:
                print(f'; bad opcode {opc:02x} at {bank}:{pc:04x}', file=sys.stderr)
                break
            name, mode, n, v = target(rom, bank, pc)
            code[(bank, pc)] = (name, mode, n, v)
            nxt = (pc + n) & 0xffff
            nbank = bank
            if mode in ('abs', 'abx', 'aby') and hotspot(v) is not None:
                nbank = hotspot(v)
            if mode == 'rel':
                t = (nxt + (v - 256 if v & 0x80 else v)) & 0xffff
                xref.setdefault((bank, t), set()).add((bank, pc))
                todo.append((bank, t))
            if name in ('jsr', 'jmp') and mode == 'abs':
                if is_rom(v):
                    tb = nbank
                    xref.setdefault((tb, ORG[tb] | (v & 0xfff)), set()).add((bank, pc))
                    todo.append((tb, v))
                if name == 'jsr' and hotspot(v) is not None:
                    # Activision style: JSR into hotspot area is a bank call
                    pass
            if name in ('rts', 'rti', 'brk') or (name == 'jmp'):
                break
            bank, pc = nbank, ORG[nbank] | (nxt & 0xfff)
    return code, xref


def sym(v, mode, name, labels):
    if v in labels:
        return labels[v]
    if mode in ('zp', 'zpx', 'zpy', 'abs', 'abx', 'aby') and v < 0x80:
        if name in WRITES or name in ('inc', 'dec'):
            if (v & 0x3f) < len(TIA_W):
                return TIA_W[v & 0x3f]
        elif (v & 0x0f) < len(TIA_R):
            return TIA_R[v & 0x0f]
    if v in RIOT:
        return RIOT[v]
    return None


def fmt_operand(name, mode, n, v, pc, bank, labels):
    if mode in ('imp',):
        return ''
    if mode == 'acc':
        return 'A'
    if mode == 'imm':
        return f'#${v:02x}'
    if mode == 'rel':
        t = (pc + 2 + (v - 256 if v & 0x80 else v)) & 0xffff
        return labels.get((bank, t), f'L{bank}{t:04x}')
    s = sym(v, mode, name, labels)
    if s is None:
        if mode in ('abs', 'abx', 'aby', 'ind') and is_rom(v):
            s = labels.get((bank, ORG[bank] | (v & 0xfff)), f'${v:04x}')
        else:
            s = f'${v:02x}' if n == 2 else f'${v:04x}'
    return {
        'zp': s, 'abs': s, 'zpx': s + ',x', 'zpy': s + ',y', 'abx': s + ',x',
        'aby': s + ',y', 'izx': f'({s},x)', 'izy': f'({s}),y', 'ind': f'({s})',
    }[mode]


def main():
    rom = Rom(open(sys.argv[1], 'rb').read())
    entries = []
    for b in (0, 1):
        for vec in (0xfffc,):
            entries.append((b, rom.word(b, vec)))
    labels = {}
    if len(sys.argv) > 2:
        for line in open(sys.argv[2]):
            line = line.split(';')[0].split()
            if len(line) >= 2:
                entries.append((int(line[0], 16), int(line[1], 16)))
    if len(sys.argv) > 3:
        for line in open(sys.argv[3]):
            line = line.split(';')[0].split()
            if len(line) == 2:
                labels[int(line[0], 16)] = line[1]
            elif len(line) == 3:
                labels[(int(line[0], 16), int(line[1], 16))] = line[2]
    code, xref = trace(rom, entries)
    covered = set()
    for (b, a), (_, _, n, _) in code.items():
        for i in range(n):
            covered.add((b, ORG[b] | ((a + i) & 0xfff)))
    for bank in (0, 1):
        print(f'\n;================ BANK {bank} ================')
        a = ORG[bank]
        while a <= ORG[bank] + 0xfff:
            lab = labels.get((bank, a))
            if (bank, a) in xref or lab:
                print(f'{lab or f"L{bank}{a:04x}"}:')
            if (bank, a) in code:
                name, mode, n, v = code[(bank, a)]
                raw = ' '.join(f'{rom.byte(bank, a + i):02x}' for i in range(n))
                opnd = fmt_operand(name, mode, n, v, a, bank, labels)
                print(f'  {a:04x}  {raw:<9} {name} {opnd}')
                a += n
            else:
                start = a
                row = []
                while a <= ORG[bank] + 0xfff and (bank, a) not in covered and len(row) < 8:
                    if a != start and ((bank, a) in xref or labels.get((bank, a))):
                        break
                    row.append(rom.byte(bank, a))
                    a += 1
                bits = ' '.join(f'{x:02x}' for x in row)
                gfx = ' '.join(f'{x:08b}'.replace('0', '.').replace('1', '#') for x in row) if len(row) <= 2 else ''
                print(f'  {start:04x}  .byte {bits}  {gfx}')
    tot = sum(1 for k in covered)
    print(f'; code bytes: {tot}', file=sys.stderr)


main()
