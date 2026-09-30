#!/usr/bin/env bash
# Prepare an isolated, locked dependency set under .deps/ by default.
# --unlocked is for upstream experiments; existing checkouts are never reset.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
deps_root="${CNA_DEPS_ROOT:-$root/.deps}"
locked=1

while [[ $# -gt 0 ]]; do
    case "$1" in
        --pin) locked=1 ;;
        --unlocked) locked=0 ;;
        --root)
            [[ $# -ge 2 ]] || { echo "--root needs a directory" >&2; exit 2; }
            deps_root="$2"; shift ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
    shift
done

mkdir -p "$deps_root"
deps_root="$(cd "$deps_root" && pwd)"

clone() {
    local name="$1" url="$2" branch="$3" key="$4"
    local dest="$deps_root/$name" sha current new_clone=0
    if [[ ! -d "$dest/.git" ]]; then
        echo "cloning $name ($branch)"
        git clone --branch "$branch" "$url" "$dest"
        new_clone=1
    fi
    if [[ $locked -eq 1 ]]; then
        if [[ -n "$(git -C "$dest" status --porcelain)" ]]; then
            echo "$dest has local changes; use a clean checkout for the locked baseline." >&2
            exit 1
        fi
        sha="$(sed -n "s/^${key}=//p" "$root/dependencies.lock")"
        [[ -n "$sha" ]] || { echo "missing $key in dependencies.lock" >&2; exit 1; }
        current="$(git -C "$dest" rev-parse HEAD)"
        if [[ "$current" != "$sha" ]]; then
            if [[ $new_clone -eq 1 ]]; then
                git -C "$dest" checkout --quiet --detach "$sha"
            else
                echo "$dest is at $current, expected $sha." >&2
                echo "Use --root with a fresh directory, or --unlocked for this checkout." >&2
                exit 1
            fi
        fi
        echo "$name: $sha"
    else
        echo "$name: $(git -C "$dest" rev-parse HEAD) (unlocked)"
    fi
}

clone cna           https://github.com/libcna/cna.git           next    CNA
clone sharp-runtime https://github.com/libcna/sharp-runtime.git next    SHARP_RUNTIME
clone easy-gl       https://github.com/libcna/easy-gl.git       develop EASY_GL
clone meta-gl       https://github.com/libcna/meta-gl.git       develop META_GL

git -C "$deps_root/cna" submodule update --init --recursive \
    third_party/SDL third_party/SDL_image third_party/SDL_mixer third_party/draco
echo "dependencies ready under $deps_root"
