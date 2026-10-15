"""scan_fields.py IMAGE.json LO HI - every load/store whose effective address falls in [LO, HI).

Resolves lui/addiu/ori constants per register, so base-register field accesses
the static data listing cannot attribute are found (hero block, save block).
Example: python tools/ramscan/scan_fields.py exe_ram.json 0x8008A4E0 0x8008A700
"""
import json, sys
# Poor man's dataflow over a flat MIPS image: track lui/addiu/ori constants per
# register, resolve load/store effective addresses, report hits in the hero
# block and the save block.
def load(path):
    d = json.load(open(path)); return int(d["addr"], 16), bytes.fromhex(d["hex"])
LOADS = {0x20:"lb",0x21:"lh",0x22:"lwl",0x23:"lw",0x24:"lbu",0x25:"lhu",0x26:"lwr"}
STORES = {0x28:"sb",0x29:"sh",0x2A:"swl",0x2B:"sw",0x2E:"swr"}
FIELDS = {0x8008A5AE:"HP",0x8008A5B0:"HPmax",0x8008A5B2:"MP",0x8008A5B4:"MPmax",0x8008A5B6:"STR",0x8008A5B8:"DEF",
          0x800A2DD0:"Val",0x800A2D9C:"playtime"}
def s16(x): return x - 0x10000 if x & 0x8000 else x
def scan(base, img, lo, hi, tag):
    regs = {}
    out = []
    n = len(img) // 4
    for i in range(n):
        w = int.from_bytes(img[4*i:4*i+4], "little"); pc = base + 4*i
        op = w >> 26; rs = (w>>21)&31; rt = (w>>16)&31; rd = (w>>11)&31; imm = w & 0xFFFF
        if op == 0x0F: regs[rt] = imm << 16; continue
        if op in (0x09, 0x08):
            regs[rt] = (regs[rs] + s16(imm)) & 0xFFFFFFFF if rs in regs else None
            if regs[rt] is None: regs.pop(rt, None)
            continue
        if op == 0x0D:
            if rs in regs: regs[rt] = regs[rs] | imm
            else: regs.pop(rt, None)
            continue
        if op in LOADS or op in STORES:
            if rs in regs:
                ea = (regs[rs] + s16(imm)) & 0xFFFFFFFF
                if lo <= ea < hi:
                    mn = LOADS.get(op) or STORES[op]
                    out.append((pc, mn, ea, rs, s16(imm), regs[rs]))
            if op in LOADS: regs.pop(rt, None)
            continue
        if op == 0: # R-type writes rd
            regs.pop(rd, None)
            if (w & 0x3F) == 0x09: regs.pop(31, None)   # jalr
            continue
        if op == 3: regs.pop(31, None); continue           # jal
        if op in (0x04,0x05,0x06,0x07,0x01,0x02): continue # branches/j
        if op == 0x10: regs.pop(rt, None); continue        # cop0
        if op == 0x12: regs.pop(rt, None); continue        # cop2
        regs.pop(rt, None)                                  # other I-type (slti, andi, xori, ...)
    return out
if __name__ == "__main__":
    base, img = load(sys.argv[1]); lo = int(sys.argv[2],16); hi = int(sys.argv[3],16)
    hits = scan(base, img, lo, hi, "")
    for pc, mn, ea, rs, off, b in hits:
        print("%08x %-4s ea=%08x %-7s base=%08x+%d (r%d)" % (pc, mn, ea, FIELDS.get(ea,""), b, off, rs))
    print("hits:", len(hits))
