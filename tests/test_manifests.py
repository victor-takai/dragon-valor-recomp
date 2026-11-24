#!/usr/bin/env python3
"""Static checks of this project's mod packages (no game needed).

  python tests/test_manifests.py

For every mods/preloaded/packages/<id>/<version>/manifest.toml:
  - the directory names match the manifest's id / version
  - every feature is default-disabled, or default-on but inert (all options off,
    or at a value listed in INERT_CHOICES)
  - every [[option]] / [[plugin]] names a declared feature
  - integer options have min <= default <= max on the step grid
  - every [[plugin]] id is registered in mods/plugins/*.c
  - feature and option descriptions fit the launcher's 512-byte buffer
Exit code 0 when all checks pass.
"""
import glob
import re
import sys
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PACKAGES = ROOT / "mods" / "preloaded" / "packages"
# Plugin ids must be registered somewhere in the executable: our plugins, or the
# framework's builtin ones (an overriding manifest such as psx.enhancement.pgxp
# keeps the framework's plugin id).
PLUGIN_SRC = "\n".join(Path(p).read_text(encoding="utf-8", errors="replace")
                       for p in glob.glob(str(ROOT / "mods" / "plugins" / "*.c*"))
                       + glob.glob(str(ROOT / "psxrecomp" / "runtime" / "src" / "mod_builtin_*.c")))
# Choice options whose value changes nothing: a default-on feature may ship
# with these as defaults (the plugin does nothing at them). Play as stays in
# the plan, like the cheats, so the in-game menu's Play as / Weapon apply at
# once instead of after a restart (the player's report, 2026-11-18).
INERT_CHOICES = {
    ("dragonvalor.heroswap", "hero"): "stage",      # each stage's own hero
    ("dragonvalor.heroswap", "weapon"): "hero",     # the hero's own weapon
    ("dragonvalor.heroswap", "moves"): "own",       # the bodies' default kit (only a body played uses it)
}


def check(manifest: Path) -> list[str]:
    errors = []
    m = tomllib.loads(manifest.read_text(encoding="utf-8"))
    pkg_id, version = manifest.parent.parent.name, manifest.parent.name
    if m.get("id") != pkg_id:
        errors.append(f"id {m.get('id')!r} != directory {pkg_id!r}")
    if m.get("version") != version:
        errors.append(f"version {m.get('version')!r} != directory {version!r}")
    features = {f["id"]: f for f in m.get("feature", [])}
    if not features:
        errors.append("no [[feature]]")
    for fid, f in features.items():
        if f.get("default_enabled", False):
            # A default-on feature is allowed only when it is inert as shipped:
            # every boolean option off and every integer option at its minimum
            # (the cheats feature stays in the plan so the in-game menu can
            # switch its options live).
            opts = [o for o in m.get("option", []) if o.get("feature") == fid]
            inert = opts and all(
                (o.get("type") == "boolean" and str(o.get("default")).lower() == "false") or
                (o.get("type") == "integer" and int(o.get("default", 0)) == int(o.get("min", 0))) or
                (o.get("type") == "choice" and INERT_CHOICES.get((m.get("id"), o.get("id"))) == o.get("default"))
                for o in opts)
            if not inert:
                errors.append(f"feature {fid}: default_enabled must be false (or every option must default to off or to an INERT_CHOICES value)")
    # The launcher copies a feature's / option's description into a 512-byte
    # buffer (recomp-ui recomp_launcher.h): a longer one is cut mid-sentence on
    # the Mods page (Play as lost its last sentence, 2026-11-18).
    for key in ("feature", "option"):
        for entry in m.get(key, []):
            n = len(entry.get("description", "").encode("utf-8"))
            if n > 511:
                errors.append(f"[[{key}]] {entry.get('id')}: description is {n} bytes; the launcher keeps 511")
    for key in ("option", "plugin", "patch", "overlay"):
        for entry in m.get(key, []):
            if entry.get("feature") not in features:
                errors.append(f"[[{key}]] names unknown feature {entry.get('feature')!r}")
    for o in m.get("option", []):
        if o.get("type") == "integer":
            lo, hi, step, d = o["min"], o["max"], o.get("step", 1), int(o["default"])
            if not (lo <= d <= hi) or (d - lo) % step:
                errors.append(f"option {o['id']}: default {d} outside {lo}..{hi} step {step}")
    for p in m.get("plugin", []):
        if not re.search(r'"%s"' % re.escape(p["id"]), PLUGIN_SRC):
            errors.append(f"plugin id {p['id']!r} is not registered in mods/plugins/*.c")
    return errors


def main() -> int:
    manifests = sorted(PACKAGES.glob("*/*/manifest.toml"))
    if not manifests:
        print("no manifests under", PACKAGES)
        return 1
    bad = 0
    for mf in manifests:
        errs = check(mf)
        rel = mf.relative_to(ROOT)
        print(f"{'FAIL' if errs else 'ok  '} {rel}")
        for e in errs:
            print("     -", e)
        bad += bool(errs)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
