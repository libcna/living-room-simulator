#!/usr/bin/env bash
# Fetches the external assets listed in assets/external/manifest.json and
# verifies each file against its recorded SHA-256 (when one is recorded).
# They are fetched rather than committed: they are large and they belong to
# somebody else; the manifest is the part that is this project's.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
target="${1:-$root/assets/external/downloads}"
mkdir -p "$target"

readarray -t rows < <(python3 "$root/scripts/manifest-tool.py" fetch-list)
ok=0; skipped=0; failed=0
for row in "${rows[@]}"; do
    IFS=$'\t' read -r name url path want <<<"$row"
    dest="$target/$path"
    mkdir -p "$(dirname "$dest")"
    if [[ -f "$dest" && -n "$want" ]]; then
        have="$(sha256sum "$dest" | cut -d' ' -f1)"
        if [[ "$have" == "$want" ]]; then skipped=$((skipped + 1)); continue; fi
        printf '  %-28s checksum mismatch, refetching\n' "$name"
    elif [[ -f "$dest" ]]; then
        skipped=$((skipped + 1)); continue
    fi
    if ! curl -fsSL --retry 3 --retry-delay 2 -A "living-room-simulator-fetch/1.0" -o "$dest.partial" "$url"; then
        printf '  %-28s FETCH FAILED (%s)\n' "$name" "$url" >&2
        rm -f "$dest.partial"; failed=$((failed + 1)); continue
    fi
    if [[ -n "$want" ]]; then
        have="$(sha256sum "$dest.partial" | cut -d' ' -f1)"
        if [[ "$have" != "$want" ]]; then
            printf '  %-28s CHECKSUM MISMATCH (got %s)\n' "$name" "$have" >&2
            rm -f "$dest.partial"; failed=$((failed + 1)); continue
        fi
    fi
    mv "$dest.partial" "$dest"
    printf '  %-28s fetched\n' "$name"
    ok=$((ok + 1))
done
echo "fetched $ok, already present $skipped, failed $failed"
[[ $failed -eq 0 ]]
