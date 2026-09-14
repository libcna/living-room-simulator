#!/usr/bin/env bash
# Renders the canonical viewpoint set (28 views) under a few lighting and weather
# conditions into screenshots/audit/ for a visual audit. Headless: runs the
# application under Xvfb with Mesa's software rasteriser unless DISPLAY is set.
#
#   scripts/capture-views.sh [--quick] [--out DIR] [--bin PATH]
#
# --quick renders at 960x540 with 512^2 textures (about 25 s per view here);
# the default is 1280x720 with the full 1024^2 textures.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
bin="$root/build/bin/cna-room"
out="$root/screenshots/audit"
width=1280; height=720; texture=1024
while [[ $# -gt 0 ]]; do
    case "$1" in
        --quick) width=960; height=540; texture=512 ;;
        --out) out="$2"; shift ;;
        --bin) bin="$2"; shift ;;
        *) echo "unknown argument $1" >&2; exit 2 ;;
    esac
    shift
done
[[ -x "$bin" ]] || { echo "no binary at $bin (build first)" >&2; exit 1; }
mkdir -p "$out"

run() {
    local name="$1"; shift
    local target="$out/$name.png"
    if [[ -z "${DISPLAY:-}" ]]; then
        SDL_VIDEODRIVER=x11 LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s "-screen 0 ${width}x${height}x24" \
            "$bin" --width "$width" --height "$height" --frames 6 --texture-size "$texture" "$@" \
            --screenshot "$target" > "$out/$name.log" 2>&1 || echo "FAILED: $name (see $out/$name.log)"
    else
        "$bin" --width "$width" --height "$height" --frames 6 --texture-size "$texture" "$@" \
            --screenshot "$target" > "$out/$name.log" 2>&1 || echo "FAILED: $name (see $out/$name.log)"
    fi
    grep -E "cna-room: frame 6" "$out/$name.log" | sed -E 's/.*frame 6 -- ([0-9.]+) ms CPU.*draws ([0-9]+) \(\+([0-9]+) shadow\).*/  '"$name"': \1 ms, \2 draws + \3 shadow/' || true
}

# Daylight, the default (cloudy) weather.
for view in entrance sofa-to-tv tv-to-sofa bookshelf window window-close material corner tv-close lamp street street-left; do
    run "day-$view" --time 12:00 --view "$view" --weather cloudy --hold-weather
done
# Direct sun through the windows (clear, midday: dapples through the street trees).
run "sun-window" --time 13:00 --view window --weather clear --hold-weather
run "sun-corner" --time 13:00 --view corner --weather clear --hold-weather
run "sun-morning-corner" --time 10:30 --view corner --weather clear --hold-weather   # the sun clear of the trees, patches on the floor
# Golden hour and dusk.
run "evening-entrance" --time 17:45 --view entrance --weather clear --hold-weather
run "dusk-street" --time 18:20 --view street --weather clear --hold-weather
# Night with the lamps and the television.
for view in entrance sofa-to-tv window street street-left bookshelf; do
    run "night-$view" --time 22:00 --view "$view" --weather clear --hold-weather
done
# Weather.
run "rain-day-street" --time 15:00 --view street --weather rain --hold-weather
run "rain-night-window" --time 21:30 --view window --weather rain --hold-weather
run "storm-night-street" --time 22:00 --view street-left --weather storm --hold-weather
run "snow-day-street" --time 11:00 --view street --weather snow --hold-weather
run "snow-day-entrance" --time 11:00 --view entrance --weather snow --hold-weather
run "overcast-day-entrance" --time 12:00 --view entrance --weather overcast --hold-weather

# Contact sheet (optional, needs Pillow).
python3 - "$out" <<'PY' || true
import glob, os, sys
try:
    from PIL import Image
except ImportError:
    sys.exit(0)
out = sys.argv[1]
files = sorted(f for f in glob.glob(os.path.join(out, "*.png")) if not f.endswith("contact-sheet.png"))
if not files:
    sys.exit(0)
thumb = (480, 270)
cols = 4
rows = (len(files) + cols - 1) // cols
sheet = Image.new("RGB", (cols * thumb[0], rows * thumb[1]), (20, 20, 20))
for i, f in enumerate(files):
    im = Image.open(f).convert("RGB").resize(thumb)
    sheet.paste(im, ((i % cols) * thumb[0], (i // cols) * thumb[1]))
sheet.save(os.path.join(out, "contact-sheet.png"))
print("contact sheet:", os.path.join(out, "contact-sheet.png"))
PY
echo "rendered into $out"
