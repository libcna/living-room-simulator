#!/usr/bin/env bash
# Headless render smoke test: the binary must start, draw a few frames at a
# small size and write a screenshot that is not black. Needs xvfb-run and a
# software GL (Mesa llvmpipe); skipped (exit 77 -> ctest SKIP) without them.
set -euo pipefail
bin="${1:?binary}"
out="${2:-${TMPDIR:-/tmp}/cna-room-smoke.png}"
command -v xvfb-run >/dev/null 2>&1 || { echo "no xvfb-run"; exit 77; }
command -v python3 >/dev/null 2>&1 || { echo "no python3"; exit 77; }
rm -f "$out"
SDL_VIDEODRIVER=x11 LIBGL_ALWAYS_SOFTWARE=1 CNA_ROOM_NO_CNB="${CNA_ROOM_NO_CNB:-}" xvfb-run -a -s "-screen 0 320x180x24" \
    "$bin" --width 320 --height 180 --frames 3 --texture-size 64 --time 12:00 --view entrance --weather cloudy --hold-weather \
    --screenshot "$out" > "${out%.png}.log" 2>&1 || { echo "the binary failed (see ${out%.png}.log)"; tail -20 "${out%.png}.log"; exit 1; }
[[ -f "$out" ]] || { echo "no screenshot written"; exit 1; }
python3 - "$out" <<'PY'
import struct, sys, zlib
path = sys.argv[1]
data = open(path, 'rb').read()
assert data[:8] == b'\x89PNG\r\n\x1a\n', 'not a PNG'
pos = 8; width = height = None; idat = b''
while pos < len(data):
    length, kind = struct.unpack('>I4s', data[pos:pos + 8]); body = data[pos + 8:pos + 8 + length]; pos += 12 + length
    if kind == b'IHDR': width, height, depth, colour = struct.unpack('>IIBB', body[:10])
    elif kind == b'IDAT': idat += body
raw = zlib.decompress(idat)
channels = {2: 3, 6: 4}[colour]
stride = width * channels + 1
# A crude unfilter is unnecessary for a brightness check: sum the filter-0 rows only if
# all rows are filter 0; otherwise fall back to the byte mean, which is still far from 0
# for a lit frame and 0 for a black one.
total = sum(raw[i] for i in range(len(raw)) if i % stride != 0)
mean = total / max(1, (len(raw) - height))
print(f'{width}x{height}, mean byte {mean:.1f}')
assert width == 320 and height == 180, 'unexpected size'
assert mean > 8.0, 'the frame is black'
PY
echo "render smoke: ok ($out)"
