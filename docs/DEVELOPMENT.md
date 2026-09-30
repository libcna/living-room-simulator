# Development

## Native setup

The supported, repeatable baseline is Linux, CMake 3.23+, Ninja, a C++23 compiler and CNA's `OPENGLES3` renderer. Ubuntu 24.04 packages used by the project:

```sh
sudo apt-get install build-essential cmake ninja-build git \
  libxss-dev libxkbcommon-dev wayland-protocols libwayland-dev libdecor-0-dev \
  libdbus-1-dev libudev-dev libibus-1.0-dev libgl1-mesa-dev libegl1-mesa-dev \
  libgles2-mesa-dev libzstd-dev libxrandr-dev libxcursor-dev libxi-dev \
  libxinerama-dev libxfixes-dev libxtst-dev libxt-dev libxv-dev libxxf86vm-dev \
  libpulse-dev libasound2-dev libdrm-dev libgbm-dev
```

From the repository root:

```sh
scripts/fetch-dependencies.sh
scripts/fetch-assets.sh
scripts/extract-assets.sh
cmake --preset dev
cmake --build --preset dev
ctest --preset unit
ctest --preset dev
```

`fetch-dependencies.sh` clones the four exact revisions from `dependencies.lock` into `.deps/`; it does not move an existing checkout to another commit. Pass `--root PATH` (or set `CNA_DEPS_ROOT`) to use a fresh dependency directory elsewhere, and configure CMake with `-DCNA_ROOT_DIR=PATH/cna`. `--unlocked` uses the current revision of an existing checkout or a newly cloned branch head for upstream experiments. Do not update the lock without a successful build and test run on the new revisions.

The preset writes to `build-dev/` and gives `ccache` a writable `build-ccache/` directory; both are ignored. `cmake --preset release` and `cmake --build --preset release` use `build-release/`. The build defaults to two jobs; increase this only when memory allows. `ccache` is optional.

The external asset fetch checks SHA-256 hashes from `assets/external/manifest.json`. The extraction tool runs `npm ci` from `tools/gltf-extract/package-lock.json`; it requires Node.js and npm. `scripts/compile-assets.sh` may build optional CNA `.cnb` files to speed later loading. The app still starts without models, but visual results will differ from the reference gallery. Asset licences and attribution are in `THIRD_PARTY_ASSETS.md`.

## Run and test

```sh
./build-dev/bin/living-room-simulator --help
./build-dev/bin/living-room-simulator --view entrance --time 22:00 --weather rain --hold-weather
ctest --preset unit
ctest --preset dev
```

The unit preset runs simulation, texture baker, reflection and cube calculations. The full preset includes a small headless render that requires Xvfb and Mesa. `scripts/capture-views.sh --quick --smoke --bin build-dev/bin/living-room-simulator` captures four useful views; see [visual testing](VISUAL_TESTING.md). Run the full capture set before merging a rendering change.

The CI workflow builds and runs the unit preset on Ubuntu. A local render review is still needed for graphics changes. The current baseline is EasyGL `OPENGLES3`; `OPENGL33` and `WEBGL2` are related renderers but must be verified for each change. Other CNA renderer families have not been established as supported here.

## WebGL2

Install and activate Emscripten, or set `EMSDK_ROOT` to an SDK directory containing `emsdk_env.sh`. Then:

```sh
scripts/fetch-assets.sh
scripts/extract-assets.sh
scripts/build-web.sh
python3 -m http.server 8000 --directory build-web/bin
```

Open `http://localhost:8000/living-room-simulator.html`. The generated `.html`, `.js`, `.wasm` and `.data` must stay together. `CNA_WEB_BUILD_DIR` overrides `build-web/`. The script uses `ccache` if present and keeps its default cache in the ignored build area. The WebGL2 path was previously built, but it has not yet been rerun against the pinned baseline documented here; verify it before publishing a new package.

For a static demo package after verification, run `python3 scripts/package-web-demo.py DESTINATION`. It reads from the same `CNA_WEB_BUILD_DIR` and splits the large `.data` file into 64 MiB pieces. The package includes `LICENSE` and `THIRD_PARTY_ASSETS.md`.
