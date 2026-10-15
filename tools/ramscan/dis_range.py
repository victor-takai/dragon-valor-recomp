"""dis_range.py IMAGE.json LO HI - MIPS listing of a RAM image range (decoder from tools/mipsdis.py).

Example: python tools/ramscan/dis_range.py exe_ram.json 0x800358A8 0x800358EC
"""
import json
import sys
from pathlib import Path
TOOLS = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(TOOLS))
from pathlib import Path
src = open(Path(__file__).resolve().parent.parent / "mipsdis.py", encoding="utf-8").read()
ns = {}; exec(src.split("\ndef collect")[0], ns); dis = ns["dis"]
def image(path):
    d = json.load(open(path)); return int(d["addr"], 16), bytes.fromhex(d["hex"])
def listing(path, lo, hi):
    base, img = image(path)
    for pc in range(lo, hi, 4):
        off = pc - base
        if off < 0 or off + 4 > len(img): continue
        w = int.from_bytes(img[off:off+4], "little")
        print("%08x  %08x  %s" % (pc, w, dis(pc, w)))
if __name__ == "__main__":
    listing(sys.argv[1], int(sys.argv[2], 16), int(sys.argv[3], 16))
