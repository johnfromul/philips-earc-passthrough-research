#!/usr/bin/env python3
"""Static trace of struct-field writers/readers in an ARM32 (Thumb-2) ELF. Read-only.

Per-function disassembly over the ELF symbol table (reliable Thumb sync), via capstone.
See docs/mat-mlp-consumer-trace.md and docs/ms12-mat-transcode-trace.md. Pass the target
as an argument; run isolated (python -I). Needs: pip install capstone.

Commands: setter | scanoff <hexoff> (imm<4096 field) | scanimm <hexoff> (large reg-indexed
offset) | callers <hexva> | sym <substr> | func <hexva> <hexlen>.
"""
import sys, struct
from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB, CS_MODE_ARM

path = sys.argv[1]
data = open(path, 'rb').read()

# --- minimal ELF32 LE parsing ---
assert data[:4] == b'\x7fELF' and data[4] == 1, "not ELF32"
e_phoff, = struct.unpack_from('<I', data, 0x1c)
e_phentsize, e_phnum = struct.unpack_from('<HH', data, 0x2a)
e_shoff, = struct.unpack_from('<I', data, 0x20)
e_shentsize, e_shnum, e_shstrndx = struct.unpack_from('<HHH', data, 0x2e)

# program headers -> VA/offset map (PT_LOAD = 1)
loads = []
for i in range(e_phnum):
    off = e_phoff + i*e_phentsize
    p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags, p_align = struct.unpack_from('<8I', data, off)
    if p_type == 1:
        loads.append((p_vaddr, p_offset, p_filesz, p_flags))

def va_to_off(va):
    for v, o, fsz, fl in loads:
        if v <= va < v + fsz:
            return o + (va - v)
    return None

def off_to_va(o):
    for v, off, fsz, fl in loads:
        if off <= o < off + fsz:
            return v + (o - off)
    return None

# section headers -> find .text range and symbol tables
sections = []
sh_str_off = None
for i in range(e_shnum):
    off = e_shoff + i*e_shentsize
    sh_name, sh_type, sh_flags, sh_addr, sh_offset, sh_size, sh_link, sh_info, sh_addralign, sh_entsize = struct.unpack_from('<10I', data, off)
    sections.append(dict(name=sh_name, type=sh_type, addr=sh_addr, offset=sh_offset, size=sh_size, link=sh_link, entsize=sh_entsize))
shstr = sections[e_shstrndx]
def secname(s):
    b = data[shstr['offset']+s['name']:]
    return b[:b.index(b'\x00')].decode('latin1')

text = None
symtabs = []
for s in sections:
    nm = secname(s)
    if nm == '.text':
        text = s
    if s['type'] in (2, 11):  # SYMTAB, DYNSYM
        symtabs.append(s)

# build symbol map (addr -> name), functions only-ish
symbols = {}  # value(va, thumb-cleared) -> name
sym_by_va = []
for st in symtabs:
    strtab = sections[st['link']]
    n = st['size'] // 16
    for i in range(n):
        o = st['offset'] + i*16
        st_name, st_value, st_size, st_info, st_other, st_shndx = struct.unpack_from('<IIIBBH', data, o)
        if st_name == 0:
            continue
        nb = data[strtab['offset']+st_name:]
        name = nb[:nb.index(b'\x00')].decode('latin1')
        va = st_value & ~1
        if st_value and name:
            symbols.setdefault(va, name)
            if (st_info & 0xf) == 2:  # STT_FUNC
                sym_by_va.append((va, st_size, name))
sym_by_va.sort()

def func_for(va):
    # nearest preceding function symbol containing va
    import bisect
    idx = bisect.bisect_right([x[0] for x in sym_by_va], va) - 1
    while idx >= 0:
        fva, fsz, fname = sym_by_va[idx]
        if fsz == 0 or fva <= va < fva + fsz:
            return fname, fva
        if fva + max(fsz,1) <= va and va - fva < 0x2000:
            return fname, fva
        idx -= 1
    return None, None

md_t = Cs(CS_ARCH_ARM, CS_MODE_THUMB); md_t.detail = True
md_a = Cs(CS_ARCH_ARM, CS_MODE_ARM); md_a.detail = True

def disasm(va, length, thumb=True):
    o = va_to_off(va)
    code = data[o:o+length]
    md = md_t if thumb else md_a
    return list(md.disasm(code, va))

cmd = sys.argv[2] if len(sys.argv) > 2 else 'setter'

if cmd == 'setter':
    print("=== SetMatMlpOnly @0x16a9d8 (Thumb) ===")
    for ins in disasm(0x16a9d8, 0xb0, thumb=True):
        print(f"0x{ins.address:x}: {ins.mnemonic:8} {ins.op_str}")

elif cmd == 'scan348':
    # scan .text for any instruction referencing immediate displacement 0x348
    tva, toff, tsz = text['addr'], text['offset'], text['size']
    hits = []
    # disassemble whole .text in thumb (covers most); also arm pass
    for mode, md in (('T', md_t), ('A', md_a)):
        code = data[toff:toff+tsz]
        for ins in md.disasm(code, tva):
            for g in ins.operands:
                # memory operand with disp 0x348
                if g.type == 3 and getattr(g.mem, 'disp', 0) == 0x348:  # ARM_OP_MEM
                    hits.append((ins.address, mode, ins.mnemonic, ins.op_str))
    seen = set()
    for addr, mode, mn, ops in sorted(hits):
        if addr in seen: continue
        seen.add(addr)
        fn, fva = func_for(addr)
        print(f"0x{addr:x} [{mode}] {mn:8} {ops:30} | {fn}+0x{(addr-fva):x}" if fn else f"0x{addr:x} [{mode}] {mn:8} {ops}")

elif cmd == 'scan348f':
    # function-by-function disassembly (reliable Thumb sync), detect disp/imm 0x348
    import collections
    hits = []
    # gather thumb bit per function from raw symtab values
    thumb_bit = {}
    for st in symtabs:
        n = st['size'] // 16
        for i in range(n):
            o = st['offset'] + i*16
            st_name, st_value, st_size, st_info, st_other, st_shndx = struct.unpack_from('<IIIBBH', data, o)
            if (st_info & 0xf) == 2 and st_value:
                thumb_bit[st_value & ~1] = st_value & 1
    for fva, fsz, fname in sym_by_va:
        if fsz == 0:
            fsz = 0x400
        o = va_to_off(fva)
        if o is None:
            continue
        thumb = thumb_bit.get(fva, 1)
        md = md_t if thumb else md_a
        code = data[o:o+fsz]
        prev_add = {}  # reg -> True if recently added 0x348
        for ins in md.disasm(code, fva):
            found = False
            kind = None
            for g in ins.operands:
                if g.type == 3 and getattr(g.mem, 'disp', 0) == 0x348:
                    found = True; kind = ins.mnemonic
                if g.type == 2 and g.imm == 0x348 and ins.mnemonic in ('add','addw','sub','mov','movw','ldr'):
                    found = True; kind = ins.mnemonic + '#imm'
            if found:
                hits.append((ins.address, fname, fva, kind, ins.mnemonic, ins.op_str))
    for addr, fname, fva, kind, mn, ops in sorted(hits):
        tag = 'SET' if 'str' in mn else ('GET' if 'ldr' in mn else kind)
        print(f"0x{addr:x} [{tag}] {mn:8} {ops:34} | {fname}+0x{addr-fva:x}")
    print(f"\n# total functions scanned: {len(sym_by_va)}; hits: {len(hits)}")

elif cmd == 'scanoff':
    target = int(sys.argv[3], 16)
    thumb_bit = {}
    for st in symtabs:
        n = st['size'] // 16
        for i in range(n):
            o = st['offset'] + i*16
            st_name, st_value, st_size, st_info, st_other, st_shndx = struct.unpack_from('<IIIBBH', data, o)
            if (st_info & 0xf) == 2 and st_value:
                thumb_bit[st_value & ~1] = st_value & 1
    hits = []
    for fva, fsz, fname in sym_by_va:
        if fsz == 0: fsz = 0x400
        o = va_to_off(fva)
        if o is None: continue
        md = md_t if thumb_bit.get(fva, 1) else md_a
        for ins in md.disasm(data[o:o+fsz], fva):
            for g in ins.operands:
                if g.type == 3 and getattr(g.mem, 'disp', 0) == target:
                    base = ins.reg_name(g.mem.base) if g.mem.base else '?'
                    if base in ('pc','sp'): continue  # literal/stack, not struct field
                    tag = 'SET' if 'str' in ins.mnemonic else ('GET' if 'ldr' in ins.mnemonic else ins.mnemonic)
                    hits.append((ins.address, tag, ins.mnemonic, ins.op_str, fname, fva))
    for addr, tag, mn, ops, fname, fva in sorted(hits):
        print(f"0x{addr:x} [{tag}] {mn:8} {ops:28} | {fname}+0x{addr-fva:x}")
    print(f"# hits (struct-base only) for +0x{target:x}: {len(hits)}")

elif cmd == 'scanimm':
    # find any instruction loading immediate == target (movw/movt/mov/add/sub) -> large struct offsets
    target = int(sys.argv[3], 16)
    thumb_bit = {}
    for st in symtabs:
        n = st['size'] // 16
        for i in range(n):
            o = st['offset'] + i*16
            st_name, st_value, st_size, st_info, st_other, st_shndx = struct.unpack_from('<IIIBBH', data, o)
            if (st_info & 0xf) == 2 and st_value:
                thumb_bit[st_value & ~1] = st_value & 1
    hits = []
    for fva, fsz, fname in sym_by_va:
        if fsz == 0: fsz = 0x400
        o = va_to_off(fva)
        if o is None: continue
        md = md_t if thumb_bit.get(fva, 1) else md_a
        insns = list(md.disasm(data[o:o+fsz], fva))
        for idx, ins in enumerate(insns):
            if ins.mnemonic in ('movw','mov','mov.w','add','addw','sub','subw','ldr'):
                for g in ins.operands:
                    if g.type == 2 and g.imm == target:
                        # look at next couple insns for ldr/str using the reg (GET/SET classification)
                        ctx = []
                        for j in range(idx, min(idx+4, len(insns))):
                            ctx.append(insns[j].mnemonic)
                        acc = 'SET' if any('str' in m for m in ctx) else ('GET' if any('ldr' in m for m in ctx[1:]) else '?')
                        hits.append((ins.address, acc, ins.mnemonic, ins.op_str, fname, fva))
    for addr, acc, mn, ops, fname, fva in sorted(hits):
        print(f"0x{addr:x} [{acc}] {mn:8} {ops:22} | {fname}+0x{addr-fva:x}")
    print(f"# sites referencing imm 0x{target:x}: {len(hits)}")

elif cmd == 'callers':
    # find bl/blx that target the given VA
    tgt = int(sys.argv[3], 16)
    thumb_bit = {}
    for st in symtabs:
        n = st['size'] // 16
        for i in range(n):
            o = st['offset'] + i*16
            st_name, st_value, st_size, st_info, st_other, st_shndx = struct.unpack_from('<IIIBBH', data, o)
            if (st_info & 0xf) == 2 and st_value:
                thumb_bit[st_value & ~1] = st_value & 1
    for fva, fsz, fname in sym_by_va:
        if fsz == 0: fsz = 0x400
        o = va_to_off(fva)
        if o is None: continue
        md = md_t if thumb_bit.get(fva, 1) else md_a
        for ins in md.disasm(data[o:o+fsz], fva):
            if ins.mnemonic in ('bl','blx','b','b.w'):
                for g in ins.operands:
                    if g.type == 1 and g.imm == tgt:
                        print(f"0x{ins.address:x} {ins.mnemonic:5} -> 0x{tgt:x} | {fname}+0x{ins.address-fva:x}")

elif cmd == 'sym':
    needle = sys.argv[3]
    for va, sz, name in sym_by_va:
        if needle.lower() in name.lower():
            print(f"0x{va:x} sz=0x{sz:x} {name}")

elif cmd == 'func':
    va = int(sys.argv[3], 16); length = int(sys.argv[4], 16) if len(sys.argv)>4 else 0x120
    for ins in disasm(va, length, thumb=True):
        print(f"0x{ins.address:x}: {ins.mnemonic:8} {ins.op_str}")
