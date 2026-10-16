# Controls

How a pad or the keyboard reaches the game, what the game does with each
PlayStation button, and the host hotkeys. Settings pages are the launcher's
(it opens when you start `DragonValorUS_Recompiled`) and the in-game menu's.

## Pads

Xbox, PlayStation and Switch pads work out of the box through SDL's
controller database; player 1 is the first pad (`[controller] p1_device =
"auto"` in `game.toml`). Buttons map by position: on an Xbox pad **A** is
Cross, **B** Circle, **X** Square, **Y** Triangle, the bumpers L1/R1, the
triggers L2/R2, **View** Select and **Menu** Start. The pad opens in analog
(DualShock) mode, so the left stick moves like the D-pad; the launcher's
controller row switches to D-pad only.

Vibration: the game's Configuration > Vibration Setup is ON by default and
drives DualShock rumble. Whether the host pad actually rumbles is still
unverified (`docs/STATUS.md`, open items).

## Keyboard

Generated into `keybinds.ini` next to the executable on the first run; edit
that file and restart, or rebind live on the launcher's Controls page
(`config.ini` `[KeyMap]` holds the host hotkeys below, not the pad buttons).

| PlayStation | Key | | PlayStation | Key |
|---|---|---|---|---|
| D-pad | Arrow keys | | L1 / R1 | `Q` / `W` |
| Cross | `X` | | L2 / R2 | `E` / `R` |
| Circle | `S` | | L3 / R3 | `T` / `Y` |
| Square | `Z` | | Start | `Enter` |
| Triangle | `A` | | Select | `Right Shift` |

## In the game (default Controller Setup)

The game's own defaults, as its Configuration > Controller Setup screen
lists them; that screen remaps them and Select there restores the defaults.

| Button | Action |
|---|---|
| D-pad / left stick | Move |
| Square | Attack (sword; grab; up to three slashes in a row) |
| Cross | Jump (twice for a double jump) |
| Circle | Magic (cast the selected spell) |
| Triangle | Status screen (Magic / Items pages, Val) |
| L1 / L2 | Dash |
| R1 / R2 | Duck |
| Start | Pause |

Combination moves (Square + Cross, dash attacks, the impale after a double
jump) are the game's; the first stage's tutorial boxes introduce them.

## Host hotkeys

| Key / pad | Action |
|---|---|
| `Escape`, or **Select + Start** on the pad | In-game menu: pause, cheats, texture filtering, scanlines, fullscreen, volume, save/load state, disc change, back to the launcher. Arrows / D-pad move, `Enter` / Cross selects, `Escape` / Circle goes back; the mouse works too (hover selects, click activates, wheel scrolls, right button goes back). |
| `F7` | Save-state slot picker: Left / Right pick the slot, `S` saves, `L` loads, `F7` / `Esc` close |
| `F8` | Rewind (hold; only when Rewind is on in the launcher's Display card) |
| `F6` | Scanlines on / off |
| Numpad `+` / `-` | Volume |
| `Tab` (hold), `F9` | Turbo (hold / toggle) |
| `Alt` + `Enter`, `Ctrl` + `F` | Fullscreen |
| `F` | FPS readout |

Save states are the framework's, not the game's: a state taken with one
build may not load after a framework bump (`docs/framework_pin_history.md`).
The game's own saves (Memory Card) live in `saves/`.
