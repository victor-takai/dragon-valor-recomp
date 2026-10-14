"""mipsdis.py FILE.c (FUNCNAME | 0xLO-0xHI) - disassemble the instruction words recorded in psxrecomp generated C.

Example: python tools/mipsdis.py generated/SLUS_010.92_full_34.c "void func_8006C250"
         python tools/mipsdis.py generated/overlays_static_0001.c 0x800BD038-0x800BD0F4

The generated C carries every instruction as a comment `/* 0xADDR: 0xWORD */`; this
collects them (per function or per address range) and prints a plain MIPS listing.
"""
import re, sys

R = ["zero","at","v0","v1","a0","a1","a2","a3","t0","t1","t2","t3","t4","t5","t6","t7",
     "s0","s1","s2","s3","s4","s5","s6","s7","t8","t9","k0","k1","gp","sp","fp","ra"]

def dis(pc, w):
    op = w >> 26; rs = (w >> 21) & 31; rt = (w >> 16) & 31; rd = (w >> 11) & 31; sa = (w >> 6) & 31
    fn = w & 63; imm = w & 0xFFFF; simm = imm - 0x10000 if imm & 0x8000 else imm
    tgt = ((pc + 4) & 0xF0000000) | ((w & 0x3FFFFFF) << 2); br = pc + 4 + (simm << 2)
    if w == 0: return "nop"
    if op == 0:
        sp = {0:"sll",2:"srl",3:"sra"}
        if fn in sp: return f"{sp[fn]} {R[rd]}, {R[rt]}, {sa}"
        sv = {4:"sllv",6:"srlv",7:"srav"}
        if fn in sv: return f"{sv[fn]} {R[rd]}, {R[rt]}, {R[rs]}"
        if fn == 8: return f"jr {R[rs]}"
        if fn == 9: return f"jalr {R[rd]}, {R[rs]}"
        if fn == 12: return "syscall"
        if fn == 16: return f"mfhi {R[rd]}"
        if fn == 18: return f"mflo {R[rd]}"
        if fn == 17: return f"mthi {R[rs]}"
        if fn == 19: return f"mtlo {R[rs]}"
        md = {24:"mult",25:"multu",26:"div",27:"divu"}
        if fn in md: return f"{md[fn]} {R[rs]}, {R[rt]}"
        al = {32:"add",33:"addu",34:"sub",35:"subu",36:"and",37:"or",38:"xor",39:"nor",42:"slt",43:"sltu"}
        if fn in al:
            if fn == 37 and rt == 0: return f"move {R[rd]}, {R[rs]}"
            return f"{al[fn]} {R[rd]}, {R[rs]}, {R[rt]}"
        return f".word 0x{w:08X}"
    if op == 1:
        n = {0:"bltz",1:"bgez",16:"bltzal",17:"bgezal"}.get(rt, f"regimm{rt}")
        return f"{n} {R[rs]}, 0x{br:08X}"
    if op == 2: return f"j 0x{tgt:08X}"
    if op == 3: return f"jal 0x{tgt:08X}"
    if op == 4: return f"beq {R[rs]}, {R[rt]}, 0x{br:08X}" if rt else f"beqz {R[rs]}, 0x{br:08X}"
    if op == 5: return f"bne {R[rs]}, {R[rt]}, 0x{br:08X}" if rt else f"bnez {R[rs]}, 0x{br:08X}"
    if op == 6: return f"blez {R[rs]}, 0x{br:08X}"
    if op == 7: return f"bgtz {R[rs]}, 0x{br:08X}"
    if op == 8 or op == 9:
        if rs == 0: return f"li {R[rt]}, {simm}"
        return f"addiu {R[rt]}, {R[rs]}, {simm}"
    if op == 10: return f"slti {R[rt]}, {R[rs]}, {simm}"
    if op == 11: return f"sltiu {R[rt]}, {R[rs]}, {simm}"
    if op == 12: return f"andi {R[rt]}, {R[rs]}, 0x{imm:X}"
    if op == 13: return f"ori {R[rt]}, {R[rs]}, 0x{imm:X}" if rs else f"li {R[rt]}, 0x{imm:X}"
    if op == 14: return f"xori {R[rt]}, {R[rs]}, 0x{imm:X}"
    if op == 15: return f"lui {R[rt]}, 0x{imm:X}"
    if op == 16: return f"cop0 0x{w:08X}"
    if op == 18: return f"cop2 0x{w:08X}"
    ld = {32:"lb",33:"lh",34:"lwl",35:"lw",36:"lbu",37:"lhu",38:"lwr",40:"sb",41:"sh",42:"swl",43:"sw",46:"swr",50:"lwc2",58:"swc2"}
    if op in ld: return f"{ld[op]} {R[rt]}, {simm}({R[rs]})"
    return f".word 0x{w:08X}"

JAL_RE = re.compile(r"cpu->gpr\[31\] = 0x([0-9A-F]{8})u;.{0,400}?CPS jal -> 0x([0-9A-F]{8})", re.S)

def add_jals(pairs, text, lo=0, hi=0xFFFFFFFF):
    """The generated C emits a jal as `gpr[31] = link; ... pc = target` without
    the instruction-word comment; rebuild the word so the listing shows it."""
    for link, tgt in JAL_RE.findall(text):
        a = int(link, 16) - 8; t = int(tgt, 16)
        if lo <= a < hi and a not in pairs:
            pairs[a] = 0x0C000000 | ((t >> 2) & 0x3FFFFFF)

def collect(path, which):
    text = open(path, encoding="utf-8", errors="replace").read()
    m = re.match(r"0x([0-9A-Fa-f]+)-0x([0-9A-Fa-f]+)$", which)
    pairs = {}
    if m:
        lo, hi = int(m.group(1), 16), int(m.group(2), 16)
        for a, w in re.findall(r"/\* 0x([0-9A-F]{8}): 0x([0-9A-F]{8}) \*/", text):
            a = int(a, 16)
            if lo <= a < hi: pairs[a] = int(w, 16)
        add_jals(pairs, text, lo, hi)
    else:
        i = text.find(which + "(CPUState* cpu)\n{")
        if i < 0: i = text.find(which + "(CPUState* cpu)\n")
        if i < 0: sys.exit(f"function {which} not found")
        j = text.find("\n}\n", i)
        for a, w in re.findall(r"/\* 0x([0-9A-F]{8}): 0x([0-9A-F]{8}) \*/", text[i:j]):
            pairs[int(a, 16)] = int(w, 16)
        add_jals(pairs, text[i:j])
    return pairs

def main():
    path, which = sys.argv[1], sys.argv[2]
    pairs = collect(path, which)
    last = None
    for a in sorted(pairs):
        if last is not None and a != last + 4: print("    ...")
        w = pairs[a]; print(f"{a:08X}  {w:08X}  {dis(a, w)}"); last = a
    print(f"; {len(pairs)} instructions")

if __name__ == "__main__":
    main()
