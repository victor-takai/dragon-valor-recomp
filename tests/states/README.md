# Test save states

Committed copies of the runtime save states the automated tests start from.
The runtime reads and writes slots under `saves/openbios/` (gitignored);
`tools/dvdbg.py state load N` copies the file from here when that slot is
missing, and `tools/dvdbg.py state keep N` copies a freshly saved slot back
here so it can be committed.

| File | Slot | Where | Notes |
|---|---|---|---|
| `state_80012EA0_disc1_slot00.pst` | 0 | Brize Forest, first screen of gameplay after the opening cutscene (HP 030/030, MP 020/020, first enemy on screen) | Taken 2026-10-14 on psxrecomp 54abcb70 with OpenBIOS, disc 1 (`SLUS-01092`); loads unchanged on f9c287f5 (verified the same day). Saves ~5 min of New Game intro per test. |

The file name is the runtime's own key: entry PC `80012EA0`, disc token,
slot number. A state is whole-machine state and is only valid for the same
BIOS (OpenBIOS) and the same disc.
