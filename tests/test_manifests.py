#!/usr/bin/env python3
"""Static checks of this project's mod packages (no game needed).

  python tests/test_manifests.py

For every mods/preloaded/packages/<id>/<version>/manifest.toml:
  - the directory names match the manifest's id / version
  - every feature is default-disabled (bundled cheats must be opt-in)
  - every [[option]] / [[plugin]] names a declared feature
  - integer options have min <= default <= max on the step grid
  - every [[plugin]] id is registered in mods/plugins/*.c
Exit code 0 when all checks pass.
"""
import glob
import re
import sys
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PACKAGES = ROOT / "mods" / "preloaded" / "packages"
PLUGIN_SRC = "\n".join(Path(p).read_text(encoding="utf-8") for p in glob.glob(str(ROOT / "mods" / "plugins" / "*.c")))


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
            errors.append(f"feature {fid}: default_enabled must be false")
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
