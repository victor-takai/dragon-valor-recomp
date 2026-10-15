"""taint_scan.py IMAGE.json... - loads through registers derived from the attack table (0x80086318 block).

Follows addiu/addu chains, so indexed reads such as attack[index] show up
(this is how pc 0x80041798 in func_80040DCC was found).
"""
import json, sys
# Registers derived from the attack-component table (0x80086318 block) through
# addiu/addu chains; report every load through such a register.
LOADS = {0x20:"lb",0x21:"lh",0x23:"lw",0x24:"lbu",0x25:"lhu"}
def s16(x): return x - 0x10000 if x & 0x8000 else x
def run(path, lo, hi):
    j = json.load(open(path)); base = int(j["addr"], 16); img = bytes.fromhex(j["hex"])
    regs = {}; taint = set(); out = []
    for i in range(len(img)//4):
        w = int.from_bytes(img[4*i:4*i+4], "little"); pc = base + 4*i
        op = w >> 26; rs = (w>>21)&31; rt = (w>>16)&31; rd = (w>>11)&31; imm = w & 0xFFFF; fn = w & 0x3F
        if op == 0x0F: regs[rt] = imm << 16; taint.discard(rt); continue
        if op in (0x09, 0x08):
            if rs in regs:
                v = (regs[rs] + s16(imm)) & 0xFFFFFFFF; regs[rt] = v
                if lo <= v < hi: taint.add(rt)
                elif rs in taint: taint.add(rt)
                else: taint.discard(rt)
            else:
                regs.pop(rt, None)
                if rs in taint: taint.add(rt)
                else: taint.discard(rt)
            continue
        if op == 0:
            if fn in (0x21, 0x20, 0x23, 0x22):   # addu/add/subu/sub
                if rs in taint or rt in taint: taint.add(rd)
                else: taint.discard(rd)
            else: taint.discard(rd)
            regs.pop(rd, None)
            if fn == 9: taint.discard(31); regs.pop(31, None)
            continue
        if op in LOADS:
            if rs in taint and not (rs in regs and lo <= (regs[rs] + s16(imm)) & 0xFFFFFFFF < hi and False):
                known = regs.get(rs)
                ea = ((known + s16(imm)) & 0xFFFFFFFF) if known is not None else None
                out.append((pc, LOADS[op], rs, s16(imm), ea))
            regs.pop(rt, None); taint.discard(rt); continue
        if op in (0x28,0x29,0x2B,0x2A,0x2E): continue
        if op == 3: taint.discard(31); regs.pop(31, None); continue
        if op in (2, 1, 4, 5, 6, 7): continue
        regs.pop(rt, None); taint.discard(rt)
    return out
if __name__ == "__main__":
    for path in sys.argv[1:]:
        print("==", path)
        for pc, mn, rs, off, ea in run(path, 0x80086318, 0x80086340):
            if ea is None:   # indexed (base known only through an addu): the interesting ones
                print("  %08x %-3s %d(r%d)  [indexed]" % (pc, mn, off, rs))
