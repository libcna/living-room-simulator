#!/usr/bin/env bash
# Compiles every extracted glTF model into a CNA .cnb (assets/cnb/<name>.cnb)
# with CNA's gltf_to_cnb tool (plus the texture sidecars it references),
# which the application loads in place of the glTF import when the .cnb is
# at least as new as its source. Builds the tool target first when it is
# missing. Idempotent: up-to-date files are skipped. Two Khronos models with
# material variants are refused by the CNB schema and stay on the glTF import.
#
#   scripts/compile-assets.sh [--force]
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
tool="$root/build/bin/cna_tool_gltf_to_cnb"
src="$root/assets/external/extracted"
out="$root/assets/cnb"
force=0
[[ "${1:-}" == "--force" ]] && force=1
if [[ ! -x "$tool" ]]; then
    echo "building cna_tool_gltf_to_cnb" >&2
    cmake --build "$root/build" --target cna_tool_gltf_to_cnb
fi
[[ -d "$src" ]] || { echo "no extracted models at $src (run scripts/extract-assets.sh)" >&2; exit 1; }
mkdir -p "$out"
compiled=0; skipped=0; failed=0
for glb in "$src"/*.glb; do
    name="$(basename "$glb" .glb)"
    cnb="$out/$name.cnb"
    if [[ $force -eq 0 && -f "$cnb" && "$cnb" -nt "$glb" ]]; then skipped=$((skipped + 1)); continue; fi
    # The .cnb absorbs the mesh buffers; its textures stay sidecar PNGs the
    # content manager resolves by name beside it (CNA_FINDINGS R-28), so
    # they are taken from the staging directory the tool can keep.
    staging="$out/.staging-$name"
    rm -rf "$staging"
    if "$tool" "$glb" "$out" "$name" --keep-cnj "$staging" > /dev/null 2> "$out/$name.log"; then
        rm -f "$out/${name}_tex"*.png
        for png in "$staging"/*.png; do [[ -f "$png" ]] && cp "$png" "$out/"; done
        compiled=$((compiled + 1)); rm -f "$out/$name.log"
    else
        failed=$((failed + 1)); echo "FAILED: $name (see $out/$name.log)" >&2
    fi
    rm -rf "$staging"
done
echo "compiled $compiled, up to date $skipped, failed $failed -> $out"
