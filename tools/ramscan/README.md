# tools/ramscan

Small scripts used to find the damage path (docs/DEVELOPMENT.md, "How damage
is computed"). They work on RAM images dumped from a running debug build, so
they see the EXE and the overlay exactly as loaded.

| Script | Does |
|---|---|
| `dump_ram.py` | saves `exe_ram.json` (0x80010000, 0x68000 bytes) and `action_ram.json` (ACTION.BIN at 0x800BBEF0) from the debug server |
| `scan_fields.py IMAGE LO HI` | every load/store into an address range, resolving lui/addiu/ori bases per register |
| `taint_scan.py IMAGE...` | loads through registers derived from the attack table, including indexed ones |
| `dis_range.py IMAGE LO HI` | MIPS listing of a range (decoder from `tools/mipsdis.py`) |
| `hit_test.py` | the swing-and-read experiment that confirmed `attack[]` is the real power |

Run them from the folder that holds the JSON dumps; they import `tools/dvdbg.py`
for the TCP debug server (port 4370).
