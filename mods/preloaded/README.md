# Preloaded mods

Ship reviewed, default-disabled packages here:

```text
packages/<package-id>/<version>/
  manifest.toml
  ...
```

Build wiring copies `mods/preloaded/packages` next to the game executable as
`mods/bundled/`. That tree is build output: every build wipes and re-stages it,
so nothing you place there by hand survives.

Player-installed `.psxmod` archives live in `mods/installed/`, which the
launcher owns and no build ever touches. Install them through the launcher Mods
manager rather than committing them here.

Native code for `[[plugin]]` ids lives in `../plugins/` (linked into the exe
by `CMakeLists.txt`), never inside a package. Current catalog:
`packages/dragonvalor.cheats` (Infinite HP, Infinite MP, a gem or scroll every N
kills), `packages/dragonvalor.skip-intro` (presses Start at the intro movie),
`packages/dragonvalor.enhancement.widescreen` (native-wide 16:9 and wider,
experimental) and the overriding `packages/psx.enhancement.pgxp` (on by
default); details in
<https://github.com/victor-takai/dragon-valor-recomp/blob/master/docs/DEVELOPMENT.md>
(*This project's packages*). This file is staged beside the game as `mods/README.md`.

See `psxrecomp/docs/MOD_PACKAGES.md`.
