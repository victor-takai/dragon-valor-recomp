# Test save states

Local copies of the runtime save states the automated tests start from.
They are **not in git** (`.gitignore`, since 2026-10-21): a state is a
snapshot of the whole console, game code, textures and sound included, so
it may not be published with the repository, and the three below were
removed from its history before it went public. A fresh clone has none:
take them again (below) or copy them from a machine that has them.
The runtime reads and writes slots under `saves/openbios/` (gitignored);
`tools/dvdbg.py state load N` copies the file from here when that slot is
missing, and `tools/dvdbg.py state keep N` copies a freshly saved slot back
here.

| File | Slot | Where | Notes |
|---|---|---|---|
| `state_80012EA0_disc1_slot00.pst` | 0 | Brize Forest, first screen of gameplay after the opening cutscene (HP 030/030, MP 020/020, first enemy on screen) | Re-taken 2026-10-14 on psxrecomp f9c287f5, **PGXP hook flavour** (`PGXP` in `CMakeLists.txt`; the base-flavour state of the same day was refused), OpenBIOS, disc 1 (`SLUS-01092`), by `tests/retake_state0.py`'s route. Saves ~5 min of New Game intro per test. |
| `state_80012EA0_disc1_slot10.pst` | 10 | Town stage, area 1 / stage 7 (`0x800A2F18`); Fire selected (level 1; Mine level 1 is the other spell), HP 38/38, MP 38/38, Val 1014, Earrings 0, gems 2 / scrolls 2, magic-kill counter 0; two enemies of 15 HP in object slots 5 and 7, the nearer one in front of the hero so one or two Fire casts kill it; 15 pickups already on the floor (coins and the like, no gem or scroll) | Taken by the player on 2026-10-15 (menu slot 11, re-taken the same day in front of an enemy) on the PGXP build, disc 1. Start point for spell tests: `tests/test_scroll_cheat.py`, `press circle` casts Fire. |
| `state_80012EA0_disc1_slot09.pst` | 9 | Chapter 2, first stage: area 3 / stage 1, the hold of a ship; hero **Kodel**, HP 36/36, MP 31/31, STR 73, DEF 0, Val 433; Heal selected (level 2), Defense 1, Fire 2, Mine 1, Stealth 1; Pendant ×2, Lithograph ×4, Ring, Mask, Harp; three pirates in the room (enemy counter 3), the big one next to the hero; every save event flag 0 | Taken by the player on 2026-10-16 (their menu slot 3, about 67 minutes of play time) on the PGXP build, disc 1. Start point for anything past chapter 1: widescreen on later stages, a second hero for RAM diffs (`DEVELOPMENT.md`, *Character and weapon*). |

The file name is the runtime's own key: entry PC `80012EA0`, disc token,
slot number. A state is whole-machine state and is only valid for the same
BIOS (OpenBIOS), the same disc and the same runtime flavour (base vs PGXP
hook build). When `dvdbg.py state load 0` is refused after a bump, re-take
it: enable the skip-intro feature in `<build>/mods/state.toml`, launch the
debug build fresh and run `python tests/retake_state0.py` (menu → New Game →
Start through the prologue and cutscene → first screen → save + keep).

## Which slots the tools touch

Menu slot numbers are 1-based, file names 0-based (menu slot 1 =
`slot00`). Keep your own states in menu slots 2 to 9.

| Menu slot | File | Who writes it |
|---|---|---|
| 1 | `slot00` | `dvdbg.py launch --state 0` / `state load 0` restore it from this directory when it is missing, a different size or older than the copy here; `tests/retake_state0.py` re-takes it on purpose. |
| 10 | `slot09` | The third test state (chapter 2, Kodel), restored from this directory the same way. |
| 11 | `slot10` | The second test state (spells), restored from this directory the same way. |
| 12 | `slot11` | `tests/test_menu.py` saves into it through the in-game menu and puts the previous file back afterwards (before 2026-10-15 the test wrote menu slot 2 and left it there). Developer A/B scripts in the scratchpad also use it. |

Nothing else writes a slot. The launcher's own save-state hotkeys (`F7`)
and the in-game menu write wherever the player points them.
