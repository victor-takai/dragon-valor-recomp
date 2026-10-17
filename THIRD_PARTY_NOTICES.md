# Third-party notices

The root [MIT license](LICENSE) covers this project's own files: `tools/`,
`scripts/`, `tests/`, `docs/`, `assets/` (the app icon), `mods/`, `patches/`, `aot/`, `seeds/`,
`codegen_setup.c`, the CMake and config files. It does not relicense the submodules, the third-party code
they carry, Dragon Valor, the recompiled game code in `generated/` (machine
output derived from the game, no licence grant from us) or the game imagery
in screenshots.

| Component | Licence / attribution |
|---|---|
| psxrecomp, the pinned recompiler and runtime | [PolyForm Noncommercial 1.0.0](psxrecomp/LICENSE); a built executable inherits its noncommercial terms |
| recomp-ui, the launcher and in-game menu | [MIT](recomp-ui/LICENSE) |
| Dear ImGui (inside recomp-ui) | [MIT](recomp-ui/src/third_party/imgui/LICENSE.txt) |
| Launcher fonts and images | [Font notices](recomp-ui/assets/common/fonts/NOTICE.md), [image notices](recomp-ui/assets/common/img/NOTICE.md) |
| OpenBIOS (PCSX-Redux) and the components it bundles | [OpenBIOS notice](psxrecomp/bios/OpenBIOS.LICENSE); the release runs on it, no retail BIOS is shipped |
| Runtime libraries: libchdr (BSD-3-Clause), libwebp, stb_image, toml11, xxhash, OpenXR SDK; libjuice (MPL-2.0) in netplay builds only | [Framework attribution](psxrecomp/THIRD_PARTY_ATTRIBUTION.md) and the [individual notices](psxrecomp/runtime/licenses) |
| recomp-net and retcomm-rbengine (netplay / rollback libraries linked by the framework) | [recomp-net](psxrecomp/lib/recomp-net/LICENSE), [retcomm-rbengine](psxrecomp/lib/retcomm-rbengine/LICENSE) |
| TinyCC, shipped in `overlay_toolchain/` to compile overlay code on the player's machine | [LGPL-2.1](psxrecomp/overlay_toolchain/tcc/COPYING) |
| Embedded Python, shipped in `overlay_toolchain/` | [PSF licence](psxrecomp/overlay_toolchain/python/LICENSE.txt) |
| Recompiler build dependencies (ELFIO, fmt, rabbitizer, toml11) | Their licences under `psxrecomp/recompiler/lib/`; build-time only, not in the release |
| Application icon | This project's (MIT): our own pixel art, a sword on a crimson field with a cyan glint, after the idea of the game's memory-card save icon (no pixel of it is used), drawn by `tools/make_app_icon.py`; embedded as `assets/psxrecomp.ico`, `assets/psxrecomp.png` beside the exe |
| Box art under `launcher_assets/img/`, when present | [libretro-thumbnails](https://github.com/libretro-thumbnails/libretro-thumbnails) (`Named_Boxarts`); see `BOXART_SOURCE.txt` next to it |

The release zip stages these texts beside the executable: `licenses/`
(the framework's `THIRD_PARTY_ATTRIBUTION.md`, `psxrecomp-LICENSE`,
`recomp-ui-LICENSE`, `TinyCC-LICENSE.txt` and the runtime `*-NOTICES.txt`),
`bios/OpenBIOS.LICENSE`, the launcher asset notices under `assets/`, and
the toolchain's `python/LICENSE.txt` and `tcc/COPYING`
(`psxrecomp/tools/package_game_release.sh`).

Public cheat-code lists were consulted only to cross-check RAM addresses
(`docs/DEVELOPMENT.md`, *Public cheat codes*); no code list is reproduced
or shipped.

Dragon Valor, its characters, art, music and game data are copyright Namco.
You must own the game; no disc data is included, and the screenshots in
`docs/` show the game only to document this project.
