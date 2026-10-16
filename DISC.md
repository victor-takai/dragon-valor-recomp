# Which disc images work

Dragon Valor (USA) Recompiled runs from **your own dumps** of the two USA discs.
Nothing from the game is included in this download.

| Disc | Serial | Redump name | Data track (Track 1) |
|---|---|---|---|
| 1 | SLUS-01092 | `Dragon Valor (USA) (Disc 1).cue` + `.bin` files | 590,288,496 bytes, SHA-256 `9b41965d1c1bd8ab909d3a5301f376aaf4be346039aae070869b018afe59c59f` |
| 2 | SLUS-01164 | `Dragon Valor (USA) (Disc 2).cue` + `.bin` files | 657,981,408 bytes, SHA-256 `6a639b8055f8b80dfec909722704d09d3e50344b7bddb4ff7dfc8c5fa4710de7` |

Track 1 digests as Redump lists them (also in `game.toml [prepare_disc]`):

| Disc | CRC32 | MD5 | SHA-1 |
|---|---|---|---|
| 1 | `16aae053` | `2c9782f6009ac474217dc748337078a8` | `110e7239892f1b7fe0848eea46fa75325cf16111` |
| 2 | `99727f4a` | `80bf8b4b01e57d63d9e49942f72b4aa7` | `15e75aee2fdb989f8ba62d9269317949e835f325` |

- Dump as a **cue + bin set with both tracks** (data + audio), Redump style.
  Keep the Redump file names: the launcher's Disc 1 / Disc 2 selection
  identifies a disc by its file name.
- On the first launch the launcher asks for the disc image; browse to the
  `.cue`. It remembers the location (`settings.toml` next to the game) and
  checks the serial, region and ISO header before Play.
- Other regions (the European SCES-01705 / SCES-11705, the Japanese release)
  are different executables and will not run on this build.
- The game runs on the bundled open-source OpenBIOS; no BIOS dump is needed.
