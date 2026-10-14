#!/usr/bin/env bash
# Thin wrapper around the shared psxrecomp bundled-release packager.
# Autofilled by psxrecomp/tools/new_project_layout/setup_project.{sh,ps1}.
#
# Ships the COMPILED game built from this repo's committed generated/ C:
# executable, runtime data, bundled OpenBIOS, mod catalog, overlay toolchain.
# No sources, emitters, CLI, generated C, or BIOS dumps.
#
# Usage:
#   scripts/package_release.sh <build-dir> <artifact-tag> [recompiler-build-dir]
#   e.g. scripts/package_release.sh build-release windows-x64
#
# Writes: dist/dvus-<VERSION>-<artifact-tag>.zip. Side effect: the packager
# rewrites the tracked VERSION file with the stamp embedded in the binary.
# On this machine it also needs PSX_RELEASE_STAGE_PYTHON=<python.exe>.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${1:-}"
ARTIFACT_TAG="${2:-}"
RECOMPILER_BUILD="${3:-build-recompiler}"

if [[ -z "${BUILD_DIR}" || -z "${ARTIFACT_TAG}" ]]; then
  echo "usage: $0 <build-dir> <artifact-tag> [recompiler-build-dir]" >&2
  exit 2
fi

PACKAGER="${ROOT}/psxrecomp/tools/package_game_release.sh"
if [[ ! -f "${PACKAGER}" ]]; then
  echo "error: missing ${PACKAGER} (psxrecomp submodule predates bundled releases -- bump it)" >&2
  exit 1
fi
EXTRA=()
# Overlay shard cache. It is compiled FROM THE DISC, so CI (no disc) cannot
# build one: the committed static AOT shard is linked into the executable and
# the bundled overlay_toolchain/ compiles the rest from the player's disc at
# runtime. A developer packaging locally with a cache can ship it instead:
#   PSX_OVERLAY_CACHE_ROOT=/path/to/cache scripts/package_release.sh ...
if [[ -n "${PSX_OVERLAY_CACHE_ROOT:-}" ]]; then
  EXTRA+=(--overlay-cache-root "${PSX_OVERLAY_CACHE_ROOT}")
else
  EXTRA+=(--ship-without-overlay-cache-because \
    "no disc at package time: the committed static AOT shard is compiled in and overlay_toolchain/ autocompiles the rest on the player's machine")
fi
# Extra docs shipped at the zip root (DISC.md tells players which dump works).
for _doc in DISC.md; do
  if [[ -f "${ROOT}/${_doc}" ]]; then
    EXTRA+=(--doc "${_doc}")
  fi
done

cd "${ROOT}"
exec bash "${PACKAGER}" \
  --root "${ROOT}" \
  --build-dir "${BUILD_DIR}" \
  --artifact "${ARTIFACT_TAG}" \
  --zip-prefix dvus \
  --exe-name DragonValorUS_Recompiled \
  --display-name "Dragon Valor (USA) Recompiled" \
  --recompiler-build "${RECOMPILER_BUILD}" \
  --version-env RELEASE_VERSION \
  --disc-hint "your legally owned Dragon Valor (USA) discs" \
  "${EXTRA[@]}"
