#!/usr/bin/env bash
# Records a short clip headlessly and assembles it into a GIF.
#
#   scripts/record-gif.sh OUT.gif SECONDS [app options...]
#
# Frames step 1/60 s (deterministic); every 5th is kept, so the GIF plays at
# 12 frames a second. 480x270 with 256^2 textures unless --width/--height/
# --texture-size are among the options. Needs Python 3 with Pillow.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
out="$1"; seconds="$2"; shift 2
frames=$(( seconds * 60 ))
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
run() {
    if [[ -z "${DISPLAY:-}" ]]; then
        SDL_VIDEODRIVER=x11 LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s "-screen 0 480x270x24" "$@"
    else
        "$@"
    fi
}
run "$root/build/bin/cna-room" --width 480 --height 270 --texture-size 256 --frames "$frames" --record "$work,5" "$@" > "$work/record.log" 2>&1 \
    || { echo "recording failed (see $work/record.log)" >&2; cat "$work/record.log" | tail -5 >&2; exit 1; }
python3 - "$work" "$out" <<'PY'
import glob, sys
from PIL import Image
work, out = sys.argv[1], sys.argv[2]
frames = [Image.open(f).convert("RGB") for f in sorted(glob.glob(f"{work}/frame-*.png"))]
if not frames: sys.exit("no frames recorded")
palette = frames[0].quantize(colors=255, method=Image.MEDIANCUT)
quantised = [f.quantize(colors=255, palette=palette, dither=Image.FLOYDSTEINBERG) for f in frames]
quantised[0].save(out, save_all=True, append_images=quantised[1:], duration=1000 // 12, loop=0, optimize=True)
print(f"{out}: {len(frames)} frames")
PY
