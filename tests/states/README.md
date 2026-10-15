# Test save states

Committed copies of the runtime save states the automated tests start from.
The runtime reads and writes slots under `saves/openbios/` (gitignored);
`tools/dvdbg.py state load N` copies the file from here when that slot is
missing, and `tools/dvdbg.py state keep N` copies a freshly saved slot back
here so it can be committed.

| File | Slot | Where | Notes |
|---|---|---|---|
| `state_80012EA0_disc1_slot00.pst` | 0 | Brize Forest, first screen of gameplay after the opening cutscene (HP 030/030, MP 020/020, first enemy on screen) | Re-taken 2026-10-14 on psxrecomp f9c287f5, **PGXP hook flavour** (`PGXP` in `CMakeLists.txt`; the base-flavour state of the same day was refused), OpenBIOS, disc 1 (`SLUS-01092`), by `tests/retake_state0.py`'s route. Saves ~5 min of New Game intro per test. |
| `state_80012EA0_disc1_slot10.pst` | 10 | Cave stage, area 1 / stage 11 (`0x800A2F18`), play time 30:27; Fire selected at level 3 and every spell at level 3 (`0x800A2E06`‥`0D` = 3), HP 32/39, MP 37/37, STR 89, Earrings 1, Val 270, gems 3 / scrolls 2, no enemy on screen | Taken by the player on 2026-10-15 (menu slot 11) on the same PGXP build, disc 1; the start point for spell tests (`press circle` casts Fire). |

The file name is the runtime's own key: entry PC `80012EA0`, disc token,
slot number. A state is whole-machine state and is only valid for the same
BIOS (OpenBIOS), the same disc and the same runtime flavour (base vs PGXP
hook build). When `dvdbg.py state load 0` is refused after a bump, re-take
it: enable the skip-intro feature in `<build>/mods/state.toml`, launch the
debug build fresh and run `python tests/retake_state0.py` (menu → New Game →
Start through the prologue and cutscene → first screen → save + keep).

## Which slots the tools touch

Menu slot numbers are 1-based, file names 0-based (menu slot 1 =
`slot00`). Keep your own states in menu slots 2 to 10.

| Menu slot | File | Who writes it |
|---|---|---|
| 1 | `slot00` | `dvdbg.py launch --state 0` / `state load 0` restore it from this directory when it is missing, a different size or older than the committed copy; `tests/retake_state0.py` re-takes it on purpose. |
| 11 | `slot10` | The second test state (spells), restored from this directory the same way. |
| 12 | `slot11` | `tests/test_menu.py` saves into it through the in-game menu and puts the previous file back afterwards (before 2026-10-15 the test wrote menu slot 2 and left it there). Developer A/B scripts in the scratchpad also use it. |

Nothing else writes a slot. The launcher's own save-state hotkeys (`F7`)
and the in-game menu write wherever the player points them.
