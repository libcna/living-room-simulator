#!/usr/bin/env bash
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
emsdk_root=${EMSDK_ROOT:-/home/robertvokac/emsdk}
build_dir="${root}/build-probe"

if [[ ! -f "${emsdk_root}/emsdk_env.sh" ]]; then
    echo "Emscripten SDK not found at ${emsdk_root}; set EMSDK_ROOT." >&2
    exit 1
fi
if ! command -v ccache >/dev/null 2>&1; then
    echo "ccache is required by the repository build rules." >&2
    exit 1
fi
if [[ ! -f "${root}/assets/external/extracted/leather-sofa.glb" ]]; then
    echo "Extracted furniture assets are missing; run scripts/fetch-assets.sh and scripts/extract-assets.sh first." >&2
    exit 1
fi

export EMSDK_QUIET=1
source "${emsdk_root}/emsdk_env.sh" >/dev/null
export CCACHE_DIR=/rv/cnaccache
export CCACHE_BASEDIR=/rv

cache="${build_dir}/CMakeCache.txt"
if [[ ! -f "${cache}" ]] ||
   ! grep -Fq "CNA_ROOM_RENDERER:STRING=WEBGL2" "${cache}" ||
   ! grep -Fq "${emsdk_root}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake" "${cache}"; then
    emcmake cmake -S "${root}" -B "${build_dir}" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_COMPILER_LAUNCHER=ccache \
        -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
        -DCNA_ROOM_BUILD_TESTS=OFF \
        -DCNA_ENABLE_VIDEO=OFF
fi

cmake --build "${build_dir}" --target living-room-simulator --parallel
echo "Web build: ${build_dir}/bin/living-room-simulator.html"
