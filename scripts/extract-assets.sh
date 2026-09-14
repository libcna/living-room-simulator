#!/usr/bin/env bash
# Runs tools/gltf-extract over assets/external/extract-recipes.json, writing
# one recentred, Draco-free, PNG-textured GLB per object into
# assets/external/extracted/. Also copies the single-object Khronos models
# through the same tool (Draco decode where needed, JPEG/WebP -> PNG).
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
downloads="$root/assets/external/downloads"
out="$root/assets/external/extracted"
tool="$root/tools/gltf-extract"
mkdir -p "$out"
[[ -d "$tool/node_modules" ]] || (cd "$tool" && npm install --no-audit --no-fund)

python3 - "$root/assets/external/extract-recipes.json" <<'PY' | while IFS=$'\t' read -r name source select; do
import json, sys
for r in json.load(open(sys.argv[1]))["recipes"]:
    print("\t".join([r["name"], r["source"], r["select"]]))
PY
    dest="$out/$name.glb"
    if [[ -f "$dest" && "$dest" -nt "$downloads/$source" && "$dest" -nt "$root/assets/external/extract-recipes.json" && "$dest" -nt "$tool/extract.mjs" ]]; then
        continue
    fi
    printf '%-24s <- %s\n' "$name" "$source"
    node "$tool/extract.mjs" "$downloads/$source" "$dest" --select "$select" --recentre | grep -E "recentred|wrote" | sed 's/^/    /'
done

for glb in "$downloads"/khronos/*.glb; do
    name="$(basename "$glb" .glb)"
    dest="$out/$name.glb"
    [[ -f "$dest" && "$dest" -nt "$glb" && "$dest" -nt "$tool/extract.mjs" ]] && continue
    printf '%-24s <- khronos\n' "$name"
    node "$tool/extract.mjs" "$glb" "$dest" --recentre | grep -E "recentred|wrote" | sed 's/^/    /'
done
echo "extracted into $out"
