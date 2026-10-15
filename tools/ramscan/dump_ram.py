"""dump_ram.py - save the live EXE and ACTION.BIN from guest RAM as {addr, len, hex} JSON.

Needs a running debug build (tools/dvdbg.py launch --state 0) in an action stage.
The other scripts in this folder read these files.
"""
import json
import sys
from pathlib import Path
TOOLS = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(TOOLS))
import dvdbg
def dump(addr, length, out):
    r = dvdbg.rpc(4370, "read_ram", addr="0x%08X" % addr, len=length)
    assert r.get("ok") and len(r["hex"]) == 2 * length, (r.get("ok"), len(r.get("hex", "")))
    json.dump({"addr": "0x%08X" % addr, "len": length, "hex": r["hex"]}, open(out, "w"))
    print(out, length, "bytes")
if __name__ == "__main__":
    dump(0x800BBEF0, 40872, "action_ram.json")   # ACTION.BIN as loaded (aot/overlays.json)
    dump(0x80010000, 0x68000, "exe_ram.json")     # the EXE, text + data
