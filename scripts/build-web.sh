#!/usr/bin/env bash
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
build_dir="${CNA_WEB_BUILD_DIR:-${root}/build-web}"
if [[ -n "${EMSDK_ROOT:-}" ]]; then
    [[ -f "${EMSDK_ROOT}/emsdk_env.sh" ]] || { echo "EMSDK_ROOT has no emsdk_env.sh" >&2; exit 1; }
    export EMSDK_QUIET=1
    source "${EMSDK_ROOT}/emsdk_env.sh" >/dev/null
fi
command -v emcmake >/dev/null 2>&1 || { echo "Activate Emscripten or set EMSDK_ROOT." >&2; exit 1; }
if [[ ! -f "${root}/assets/external/extracted/leather-sofa.glb" ]]; then
    echo "Extracted furniture assets are missing; run scripts/fetch-assets.sh and scripts/extract-assets.sh first." >&2
    exit 1
fi

launcher=()
if command -v ccache >/dev/null 2>&1; then
    export CCACHE_DIR="${CNA_WEB_CCACHE_DIR:-${root}/build-web-ccache}"
    launcher=(-DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
fi

cache="${build_dir}/CMakeCache.txt"
if [[ ! -f "${cache}" ]] ||
   ! grep -Fq "CNA_ROOM_RENDERER:STRING=WEBGL2" "${cache}"; then
    emcmake cmake -S "${root}" -B "${build_dir}" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        "${launcher[@]}" \
        -DCNA_ROOM_RENDERER=WEBGL2 \
        -DCNA_ROOM_BUILD_TESTS=OFF \
        -DCNA_ENABLE_VIDEO=OFF
fi

cmake --build "${build_dir}" --target living-room-simulator --parallel "${CNA_BUILD_JOBS:-2}"
echo "Web build: ${build_dir}/bin/living-room-simulator.html"
