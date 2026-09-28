#!/usr/bin/env bash
# Headless render smoke test: the binary must start, draw a few frames at a
# small size and write a screenshot that is not black. Needs xvfb-run and a
# software GL (Mesa llvmpipe); skipped (exit 77 -> ctest SKIP) without them.
set -euo pipefail
bin="${1:?binary}"
out="${2:-${TMPDIR:-/tmp}/living-room-simulator-smoke.png}"
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
assert depth == 8, 'expected 8-bit RGB or RGBA'
stride = width * channels
assert len(raw) == height * (stride + 1), 'unexpected PNG scanline data'
previous = bytearray(stride)
total = 0
def paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    return a if pa <= pb and pa <= pc else b if pb <= pc else c
for y in range(height):
    offset = y * (stride + 1)
    kind = raw[offset]
    assert kind in range(5), 'unexpected PNG filter'
    row = bytearray(raw[offset + 1:offset + stride + 1])
    for i in range(stride):
        left = row[i - channels] if i >= channels else 0
        above = previous[i]
        upper_left = previous[i - channels] if i >= channels else 0
        predictor = (0, left, above, (left + above) // 2, paeth(left, above, upper_left))[kind]
        row[i] = (row[i] + predictor) & 255
    total += sum(row[i] for i in range(stride) if i % channels < 3)
    previous = row
mean = total / max(1, width * height * 3)
print(f'{width}x{height}, mean RGB {mean:.1f}')
assert width == 320 and height == 180, 'unexpected size'
assert mean > 8.0, 'the frame is black'
PY
echo "render smoke: ok ($out)"
