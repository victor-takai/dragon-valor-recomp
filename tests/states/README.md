# Test save states

Committed copies of the runtime save states the automated tests start from.
The runtime reads and writes slots under `saves/openbios/` (gitignored);
`tools/dvdbg.py state load N` copies the file from here when that slot is
missing, and `tools/dvdbg.py state keep N` copies a freshly saved slot back
here so it can be committed.

| File | Slot | Where | Notes |
|---|---|---|---|
| `state_80012EA0_disc1_slot00.pst` | 0 | Brize Forest, first screen of gameplay after the opening cutscene (HP 030/030, MP 020/020, first enemy on screen) | Re-taken 2026-10-14 on psxrecomp f9c287f5, **PGXP hook flavour** (`PGXP` in `CMakeLists.txt`; the base-flavour state of the same day was refused), OpenBIOS, disc 1 (`SLUS-01092`), by `tests/retake_state0.py`'s route. Saves ~5 min of New Game intro per test. |

The file name is the runtime's own key: entry PC `80012EA0`, disc token,
slot number. A state is whole-machine state and is only valid for the same
BIOS (OpenBIOS), the same disc and the same runtime flavour (base vs PGXP
hook build). When `dvdbg.py state load 0` is refused after a bump, re-take
it: enable the skip-intro feature in `<build>/mods/state.toml`, launch the
debug build fresh and run `python tests/retake_state0.py` (menu → New Game →
Start through the prologue and cutscene → first screen → save + keep).
