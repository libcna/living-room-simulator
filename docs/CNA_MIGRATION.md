# Migration to current CNA

The simulator no longer expects CNA's retired graphics engine layer. It builds
against the sibling `../cna` and `../sharp-runtime` checkouts without modifying
their source files. `dependencies.lock` records the revisions verified by this
migration; the default build uses the checkouts as they stand.

The necessary engine implementations were adapted from CNA commit
`7301f386ac50b14aa82814f04cb289ccf5ad7ce0`, immediately before retirement commit
`5572f3ca1`. They now belong to `CnaRoom::Effects`, with headers under
`include/CnaRoom/Effects/` and implementations under `src/Effects/`. Their original
Ms-PL SPDX headers and license are retained. Generated shader payloads are kept
alongside the implementations so builds do not need a shader toolchain.

This keeps atmospheric sky, environment convolution, cascaded and cube shadows,
depth/normal prepass, HDR postprocessing, exposure metering, decals and contact
shadows available. The application and its existing cube tests use these local
effects. Portable `ShaderCodeEXT` and `ShaderPackageEXT` remain CNA types and
still require `CNA_CNAEXT=ON`; PBR materials and model import remain CNA APIs.

The retired engine had private exceptions to XNA resource rules. The simulator
now respects those rules:

- The complete cascade atlas stays within both the active graphics profile's
  texture limit and the renderer's limit. With three cascades on HiDef, requests
  for 2048 or 4096 texels per cascade are reduced to 1024 texels.
- Fullscreen HDR passes use point sampling for float and half-float textures.
  Bloom upsampling uses its shader's bilinear fallback. Other resampling passes
  can look different from the historical linearly filtered rendering baseline.
  GLSL ES postprocess samplers explicitly use high precision for HDR values.
  Existing application-side shader sampling stays in place.

Current EasyGL PBR rendering also produced non-finite scene samples from
imported furniture. The observation and reproduction are recorded in
`CNA_FINDINGS.md`; its root cause remains unresolved. The simulator's own
`FiniteHdrPass` contains those pixels before bloom and other spatial filters,
and CPU probe capture keeps them out of irradiance convolution. This pass is
new MIT-licensed simulator code; the adapted engine files retain Ms-PL.

Build output and the vendored SDL build cache live under the simulator's
`build/`. Compilation and vendored dependency builds use at most two jobs and
the shared ccache at `/rv/cnaccache` with `CCACHE_BASEDIR=/rv`.

External models are prepared with `scripts/fetch-assets.sh` and
`scripts/extract-assets.sh`. They remain excluded from version control. Findings
in current upstream dependencies, if encountered, are recorded in
`CNA_FINDINGS.md`; dependency fixes are outside this migration.

## Verification (2026-09-28)

- Release / GCC 14 / OPENGLES3 build succeeded against CNA HEAD `c90f0e39f`
  with its concurrent local sensor/window changes, and sharp-runtime `6c4a857de`.
  The graphics sources match the initial CNA
  revision `8d56fa2fa` used to investigate the rendering observation.
- All five CTest tests passed: simulation, bakers, reflections, cubes and the
  complete 320×180 render smoke test. The smoke PNG's mean RGB was 71.16/255;
  the check reconstructs PNG filters and excludes alpha so a black RGBA frame
  cannot pass. The final working-tree test run took 21.98 seconds; its log is
  `build/test-latest.log`.
- The complete 640×360 nighttime entrance view at 22:00 rendered with lamps,
  cube shadows, reflections, finite probe irradiance and finite postprocess
  output. Its mean RGB was 74.32/255. The final build's image and diagnostics
  are `build/night-latest.png` and `build/night-latest.log`.
- All 54 required model assets were prepared; runtime loaded 60 imported
  placements without model-load failures. Rendering was checked on Mesa
  llvmpipe with `LP_NUM_THREADS=2` under private Xvfb displays.

The desktop window was also checked at 640×360, using the following command:

```sh
LP_NUM_THREADS=2 SDL_VIDEODRIVER=x11 ./build/bin/living-room-simulator \
  --width 640 --height 360 --texture-size 128 --view entrance --time 12:00 \
  --weather clear --hold-weather --log-every 60
```

A rebuild encountered a syntax error in concurrent uncommitted CNA
working-tree changes. This is recorded separately in `CNA_FINDINGS.md`.
Subsequent external edits removed it, and the latest rebuild succeeded.
No CNA or sharp-runtime source files were edited by this migration.
`build/` was the only main build tree, with the SDL
cache under `build/cna-sdl-prebuilt-Linux-x86_64-GNU/`. Its three nested build
trees are `SDL/build/`, `SDL_image/build/` and `SDL_mixer/build/`.
All compilation used at
most two jobs, including configure-time vendored SDL builds via
`CMAKE_BUILD_PARALLEL_LEVEL=2` and `CNA_MAX_VENDORED_BUILD_JOBS=2`.
