#!/usr/bin/env bash
# Clones the sibling checkouts cna-room builds against, next to this repository:
#
#   ../cna            (branch next)
#   ../sharp-runtime  (branch next)
#   ../easy-gl        (branch develop)
#   ../meta-gl        (branch develop)
#
# and initialises CNA's vendored submodules (SDL3, SDL3_image, SDL3_mixer, draco).
# Pinned revisions are in dependencies.lock; pass --pin to check them out.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
parent="$(cd "$root/.." && pwd)"
pin=0
[[ "${1:-}" == "--pin" ]] && pin=1

clone() {
    local name="$1" url="$2" branch="$3" key="$4"
    local dest="$parent/$name"
    if [[ ! -d "$dest/.git" ]]; then
        echo "cloning $name ($branch)"
        git clone --branch "$branch" "$url" "$dest"
    else
        echo "$name already present at $dest"
    fi
    if [[ $pin -eq 1 ]]; then
        local sha
        sha="$(grep -E "^$key=" "$root/dependencies.lock" | cut -d= -f2)"
        [[ -n "$sha" ]] && git -C "$dest" checkout --quiet "$sha" && echo "  pinned $name to $sha"
    fi
}

clone cna           https://github.com/libcna/cna.git           next    CNA
clone sharp-runtime https://github.com/libcna/sharp-runtime.git next    SHARP_RUNTIME
clone easy-gl       https://github.com/libcna/easy-gl.git       develop EASY_GL
clone meta-gl       https://github.com/libcna/meta-gl.git       develop META_GL

git -C "$parent/cna" submodule update --init --recursive \
    third_party/SDL third_party/SDL_image third_party/SDL_mixer third_party/draco
echo "dependencies ready under $parent"
