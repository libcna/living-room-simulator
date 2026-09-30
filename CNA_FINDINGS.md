# CNA findings from living-room-simulator

This is a historical issue log. See [current maintenance issues](docs/KNOWN_ISSUES.md)
for work to take now. Historical screenshot paths below refer to files moved to
`docs/archive/screenshots/` unless they remain in the curated gallery.

The status table below is the current state of every finding. The numbered findings are historical. CNA's retired engine implementations
now live in the simulator as `CnaRoom::Effects`; those entries are not claims
about the current CNA API. Migration details are in
[CNA_MIGRATION.md](docs/CNA_MIGRATION.md).

## Status against CNA `next` (2026-09-30)

The upstream status was reviewed on 2026-09-30. A subsequent simulator render against the
locked revisions still found non-finite source pixels; the upstream PBR fix did not resolve
every trigger in this scene. The historical entries below are left as written. "Retired" means CNA removed the class on
2026-09-27 (`5572f3ca1`, MOD-RETIRE-1); the simulator carries its own copies as `CnaRoom::Effects`.

| Finding | Status | CNA commit |
|---|---|---|
| Non-finite EasyGL PBR pixels | Upstream fixed known zero-normal and tangent causes, but the locked CNA baseline still gives 2318–2351 invalid raw pixels in the 320×180 render smoke scene. `FiniteHDR` reduces this to zero before bloom and must stay until the remaining cause is isolated. Other renderer families: CNA GSC-0009, open | `6924ca040`, with R-1 |
| Build diagnostics | GCC 14 false positives from libstdc++ inlining at -O3 (the same appear in about ten CNA files); no code change | — |
| Concurrent compile failure | Transient, an uncommitted edit in the sibling checkout | — |
| R-1 | Fixed: the fallback tangent is perpendicular to the normal; `ModelLibrary::repairTangents` was removed. Restoring it temporarily produced the same raw invalid-pixel counts (2318, 2331, 2351, 2350) and identical rendered pixels in the smoke scene | `8e2096c64` |
| R-2 | Fixed: `TextureCube::GetData` no longer uses a framebuffer | `8e36b7da6` |
| R-3 | Fixed on EasyGL: a double-sided back face is shaded with its basis reversed, mirrored Worlds included. Other families: CNA GSC-0010, open | `a1414a1b0` |
| R-4, R-8, R-9, R-11, R-13, R-14, R-17, R-18, R-19, R-20, R-22, R-26, R-31, R-32, R-35, R-37 | Moot: the class was retired | — |
| R-5, R-10, R-12, R-36 | Not CNA defects: material setup, what non-premultiplied blending means, and documented `PbrEffect` limits | — |
| R-6, R-7, R-16, R-27 | Deliberate limits (GLTF-339, GLTF-206, required extensions, plan_cnb D9) | — |
| R-15 | Works | — |
| R-21 | Fixed: half and float cubes store and read back in their own format, so `FloatCubeUploader` is no longer needed; `ColorSrgbEXT` is FNA's, not an XNA format | `742e30972`, `8e36b7da6`, `3b72dba63`, `01c0862fc` |
| R-23 | Fixed: SpriteBatch draws into a bound cube face | `39daf1ab7` |
| R-24 | Not a CNA defect: GLSL ES declares samplers lowp by default | — |
| R-25 | Fixed by SOFTWARE-336: stock programs use XNA's [0, 1] depth. The simulator's planar reflection capture uses stock PBR effects and now places its oblique near plane at NDC 0; the CPU reflection tests and four-view render review pass. Raw-GLSL effects still use the GL convention and need separate treatment if they are added to the capture | `7400285c3`; docs `252f20eb2` |
| R-28 | Not a defect: `.cnb` references are relative to the compile-time content root, now documented | `78b708bcb` |
| R-29 | Fixed: one `Texture2D` per image per model, V1/.cnj and V2; `remip`'s content-signature sharing can be simplified | `e45faa1d7`, `87db4ddc7` |
| R-30 | Documented contract (`ShaderEffect.hpp`); the retired `DepthNormalPrepass` was what tripped it | — |
| R-33 | Does not reproduce on current CNA: point and linear sampler states reach a `ShaderEffect` indexed draw per unit; the `texelFetch` workaround can go | test `e2972f43d` |
| R-34 | Fixed: the matrix-upload docs describe XNA's field order | `4b372c970` |

## Current build diagnostics (2026-09-28)

Building CNA `8d56fa2fa` with GCC 14 in Release mode emitted the following
diagnostics. These are recorded as unconfirmed compiler warnings, not confirmed
bugs; no changes were made to CNA or sharp-runtime.

- `modules/graphics/src/Internal/DibBitmap.cpp:91`, `WithBitmapFileHeader`:
  `-Wfree-nonheap-object` while inlining `std::vector<uint8_t>::push_back`.
- `modules/content/src/Cnb/CnbModelCodec.cpp`: `-Wstringop-overread` in the
  standard library comparison of `std::vector<uint8_t>` keys.
- `modules/content/src/GltfImport/GltfImportCore.cpp:2697`, `CloseLineLoop`:
  `-Wstringop-overflow` while inlining the vector copy.
- Vendored Draco's PLY readers/writers emit deprecated implicit `this` capture
  and `-Wstringop-overread` diagnostics. This run loads GLB models, not PLY files.

The build log is `build/build.log`. The simulator also triggers existing
overloaded virtual and conversion warnings in upstream public headers under its
stricter warning flags. These diagnostics were left visible.

## Current runtime observation: non-finite EasyGL PBR pixels

On CNA `8d56fa2fa` / sharp-runtime `6c4a857de`, OPENGLES3 with Mesa
llvmpipe produces non-finite RGB samples in the imported furniture's PBR
scene. At 320×180 the default entrance view contained 2331 such pixels out of
57600 before postprocessing. Bloom spread them across the whole frame, yielding
a completely black screenshot; CPU probe convolution also produced NaN
irradiance. This is a reproducible rendering observation; the underlying cause
has not been isolated sufficiently to attribute it to a particular upstream
function or to sharp-runtime.

The samples also occur with shadows, IBL, probes, reflections, decals, contact
shadows and normal maps disabled. A scene containing only the simulator's
procedural geometry was finite. CPU readback of the imported vertex buffers
showed finite unit normals. Replacing the imported normal/tangent basis and
disabling material maps did not remove the observation. The temporary diagnostic
overrides were removed after investigation. No upstream sources were changed.

Reproduce and inspect the raw scene with the final binary:

```sh
SDL_VIDEODRIVER=x11 LIBGL_ALWAYS_SOFTWARE=1 LP_NUM_THREADS=2 CNA_ROOM_TRACE_POST=1 \
  xvfb-run -a -s '-screen 0 320x180x24' ./build/bin/living-room-simulator \
  --width 320 --height 180 --frames 1 --texture-size 64 --time 12:00 \
  --view entrance --weather cloudy --hold-weather --screenshot build/hdr-check.png
```

`post trace scene` reports the original non-finite count. The simulator contains
the damage locally: an EasyGL fullscreen `FiniteHDR` pass replaces non-finite
pixels with finite neighbours before spatial filters, and probe capture applies
the same rule before CPU convolution. Finite samples are kept unchanged. This
is a consumer workaround, not an upstream fix. Other shader languages retain
their existing path. `CNA_ROOM_TRACE_POST` adds synchronous readback for diagnosis
and should be omitted during normal use.

## Concurrent CNA working-tree compile failure

After a successful build on CNA HEAD `c90f0e39f`, unrelated local sensor and
window changes appeared in the sibling checkout during this task. A subsequent
build failed in `modules/runtime/src/Game.cpp:492`:

```text
error: expected '}' before 'else'
```

`Game::setIsMouseVisibleProperty` contained a bare `else` immediately inside the
`if (GraphicsDevice_.GetPlatformWindowInternal() != nullptr)` block. The new
keyboard-accelerometer/orientation fallback was inserted there without a
matching `if`. This is in the uncommitted working tree, not in the recorded HEAD.
The full compiler output is retained in `build/build-concurrent-failure.log`.
No dependency edits or resets were performed. Later external edits removed
the misplaced `else`; rebuilding the latest working tree then succeeded.
The failed compiler output is kept as a record of the transient failure.

## Historical findings

Bugs, surprising behaviours and limitations of CNA (`next` @ `1b3151f2f`, EasyGL renderer on
Mesa llvmpipe ES 3.2) met while building this project, with the evidence and the workaround
used here. Numbered so `plan.md` and commit messages can refer to them. "Bug" means the
behaviour contradicts CNA's own documentation or produces wrong pictures from valid input;
"limitation" means documented or defensible behaviour that still cost a workaround.

| # | Area | Kind | Summary | Workaround in living-room-simulator |
|---|---|---|---|---|
| R-1 | glTF import, `ComputeTangentsEXT` | bug | Fallback tangent (1,0,0) for degenerate-UV triangles can be parallel to the normal; the shader's Gram-Schmidt step normalises a zero vector and the NaN normal turns the surface into a full mirror | `ModelLibrary::repairTangents` re-uploads every imported vertex buffer |
| R-2 | EasyGL `TextureCube::GetData` | bug | Framebuffer readback of a cube that has already been bound for sampling reports incomplete and throws; `EnvironmentProcessor::generateIrradiance` on a reused cube crashes the app | A fresh `TextureCube` per bake (`SkySystem::bakeEnvironment`, `SceneRenderer::finishProbe`) |
| R-3 | EasyGL `PbrEffect` | bug | Double-sided materials never flip the normal for back faces (no `gl_FrontFacing` in the renderer); the back of a leaf or curtain is lit as if facing away | Foliage drawn single-sided; noted for curtains |
| R-4 | `SsaoPass` | limitation | `radius` is a fraction of the screen; depth bias and range are fractions of the prepass far plane (0.005 / max(radius/4, 0.01)) — a 400 m far plane gives 2 m biases and streaky halos in a 6 m room | Prepass far plane 40 m, radius 0.03 |
| R-5 | glTF import, occlusion | limitation | A material without `occlusionTexture` leaves the slot empty; binding a packed metallic-roughness map there (red channel empty in glTF-Transform palettes) blacks out ambient | `Material::ormHasOcclusion` |
| R-6 | glTF import, transmission | limitation | `KHR_materials_transmission` becomes alpha = 1 − factor, so factor-1 glass vanishes (documented as GLTF-339) | Alpha floor 0.30; TV screen overridden |
| R-7 | glTF import, mips | limitation | Imported PNG/JPEG textures arrive with one level (GLTF-206) | `ModelLibrary::remip` rebuilds chains (~2 s) |
| R-8 | `FullscreenPass` | bug | Output is vertically mirrored relative to the sprite UV origin (cna-street CNA-F7), confirmed on this revision | Sky shader samples with `uFlipV = 1` |
| R-9 | `RenderPipeline` HDR target | limitation | An effect encoding sRGB into the RGBA16F scene target is encoded twice by the tonemapper (cna-street CNA-F8) | `setEncodeOutputToSrgbEXTProperty(false)` while the scene target is bound |
| R-10 | `BlendState::NonPremultiplied` + `PbrEffect` | limitation | The whole lit output is weighted by alpha, so a 4 % pane keeps 4 % of its reflection | Glass drawn with `BlendState::AlphaBlend`, black base colour (`Material::reflectiveBlend`) |
| R-11 | `CascadedShadowMap` / `CubeShadowMap` caster effects | limitation | No alpha test: masked foliage casts the shadow of its whole mesh | Accepted (dense canopies) |
| R-12 | `PbrEffect` punctual lights | limitation | One `PunctualLightEXT` per draw | Exterior built in 12 m chunks; nearest lamp per item |
| R-13 | `EnvironmentProcessor::generateBrdfLut` | limitation | 8-bit table; bias quantised to 1/255 | Accepted |
| R-14 | `EnvironmentProcessor` irradiance | limitation | CPU-side integration reads the cube back through a framebuffer (slow on every rebake, and the trigger of R-2) | Rebakes spread over frames; 48 px sky cube |
| R-15 | Half-float readback | works | `RenderTarget2D` HalfVector4 + `GetData(HalfVector4*)` reads back correctly on llvmpipe (detected at run time, 8-bit fallback kept) | — |
| R-16 | glTF import, `extensionsRequired` | limitation | Files requiring sheen/clearcoat/iridescence/anisotropy/dispersion are refused (by design) | Extractor drops those extensions |
| R-17 | `RenderPipeline` SSR | observation | With `setSSREnabled(true)` a rainy night view turned the white window frames pink/violet and the tree canopies into black noise (`docs/archive/screenshots/m7-ssr-rain-night.png`); by day on the glossy TV screen it added nothing visible over the probe reflection | Left off (`--ssr` stays an opt-in) |
| R-18 | `RenderPipeline::getSceneTarget` | limitation | Returns null once `end()` has run, so an image-based exposure has to measure the scene before the transparent phase and post chain | Measured before `end()` |
| R-19 | `AutoExposureEXT` | works | Compute-shader log-average luminance reads back on llvmpipe (ES 3.2); the key value must be calibrated per scene (0.05 here: a lamp-lit room's log-average is ~0.0006 in scene units, a sunlit one ~0.075) | Analytic schedule as prior, measurement clamped to ×/÷1.8 of it |
| R-21 | `TextureCube` formats | limitation | `TextureCube` rejects `SurfaceFormat::ColorSrgbEXT`, uploads `SetData` as RGBA8 whatever the format, and `GetData` is `Color`-only, so CNA's IBL products are linear 8-bit under one shared `Intensity`: at night the room's diffuse irradiance is ~1/300 of the shade peak that sets the scale, below one code, and the rounding of R/G/B to 0 or 1 paints the ambient magenta (`docs/archive/screenshots/m9-night-irradiance-8bit-vs-half.png`) | `FloatCubeUploader`: irradiance and prefiltered specular (one cube per roughness class, the material picks its class; the shader samples level 0 of a one-mip cube) convolved on the CPU, RGBE-encoded into a `Texture2D` strip and decoded by a draw into each face of a `HalfVector4` `RenderTargetCube` (self-tested by readback at start-up); the 8-bit path stays as the fallback |
| R-23 | `SpriteBatch` / `FullscreenPass` into a cube face | bug | With a `RenderTargetCube` face bound, `Clear` lands on the face but a `SpriteBatch` quad (and so `FullscreenPass::drawOverCurrentTarget`) draws nothing: the sprite projection is built from `GetCurrentRenderTarget2DSize`, which reports no target for a cube binding, so the quad is laid out for the back buffer | Face fills drawn as a clip-space quad through `DrawIndexedPrimitives` with a `ShaderEffect` |
| R-25 | EasyGL depth convention | limitation | XNA's projection matrices map the near plane to NDC z = 0 and EasyGL's shaders do `gl_Position = WVP * position` unchanged, so on GL (clip volume −1..1) the depth buffer only uses its upper half (a precision cost in large scenes) and clipping happens well inside the XNA near plane; a custom oblique near plane has to target −1, not the 0 the matrix suggests | Planar reflections build their oblique projection for NDC −1 |
| R-27 | `gltf_to_cnb` | limitation | Models with `KHR_materials_variants` (Khronos GlamVelvetSofa, SheenChair) are refused by the CNB v1 model schema (plan_cnb D9) while the runtime glTF import takes them | Those two stay on the glTF import; `scripts/compile-assets.sh` reports them |
| R-28 | `.cnb` texture references | note | A compiled model's textures are sibling assets resolved against the loading `ContentManager`'s root, not the `.cnb`'s directory: a `.cnb` in `assets/cnb/` loaded through the manager rooted at `assets/` fails on `<model>_tex0.png` | A second `ContentManager` rooted at `assets/cnb` for the compiled models |
| R-29 | `.cnb` model textures | limitation | A compiled model gives every part its own `Texture2D` for a shared sidecar image (the content manager's asset cache does not dedupe them), so a five-part sofa holds five copies of its palette texture: memory and any per-texture work (a mip rebuild) multiply with the part count | `ModelLibrary::remip` shares rebuilt textures by a content signature (117 copies shared across the room's models) |
| R-30 | `ShaderEffect` matrices | trap | `ShaderEffect::setWorldProperty` / `setViewProperty` / `setProjectionProperty` store the matrix and the draw call forwards it only to uniforms named `World` / `View` / `Projection`; a program that names them otherwise (CNAEXT's own `DepthNormalPrepass` effect reads `uWorld`) silently keeps its previous value, so a scene drawn through the prepass with `setWorldProperty` per object had every placed model at the origin, and the SSAO and any depth consumer read a wrong scene | `SetUniformMat4("uWorld", ...)` per object, as the shadow casters already did; a warning when a set matrix property matches no uniform would have caught it |
| R-31 | `RenderPipeline` colour grade | limitation | `RenderPipelineSettings` can enable the colour-grading pass and set its strength, but the pipeline exposes neither its `ColorGradePass` nor a way to hand it a LUT (`setLut` / `setVolumeLut` are on the pass, which is private), so through the pipeline the grade only ever copies its input | the room's white balance is a per-channel gain multiplied over the finished frame (the vignette pass); a `setColorGradeLut(Texture2D*)` on the pipeline would let the pass do its job |
| R-32 | `SsaoPass` depth bias and range | limitation | The occlusion test's depth bias (0.005) and range (a quarter of the screen radius, at least 0.01) are fixed fractions of the prepass far plane, with no setter: with a 40 m far plane an occluder must sit 0.2 to 0.4 m in front of the surface to count, so a sofa on a floor casts almost no ambient shadow (a 0.25/255 mean change over the frame at the default strength, 160 ms of llvmpipe for it); the same far plane feeds the depth of field, so the two effects pull it in opposite directions | the room's prepass far plane comes down to 16 m (`--prepass-far`), which brings the bias to 8 cm; a bias and range in world units on the pass would free the far plane for the lens |
| R-33 | `GraphicsDevice` sampler states with custom effects | limitation | `SamplerStates[n] = SamplerState::PointClamp` before a `DrawIndexedPrimitives` that draws with a `ShaderEffect` never reaches GL on EasyGL: the sampler trace (`CNA_EASYGL_SAMPLER_TRACE=1`) shows only the stock draws' Linear/Anisotropic states applied, so a custom pass samples every unit with whatever the last stock draw left there. The room's sunbeam march reads the packed RGBA8 prepass depth bilinearly as a result (a decode error at depth edges, tolerable for a march end); a pass that needs point sampling has no way to ask for it. Workaround: sample with `texelFetch` (integer texel coordinates bypass the sampler), which is what the room's sunbeam pass now does for both the depth and the atlas. |
| R-34 | `ShaderEffect::SetUniformMat4Array` layout | documentation | The setter's doc asks for "column-major" matrices, but `Matrix::ToColumnMajor` is a straight sequential copy of XNA's row-major storage and the single-matrix setter takes `&matrix.M11` as it is; both end up as the same bytes and the shader computes `mat * vec4(p, 1)` as XNA's row-vector product. Passing a genuinely transposed matrix breaks the lookup. The doc should say "the matrix's memory as is". |
| R-35 | `GpuTimer` on EasyGL over llvmpipe | limitation | A `GpuTimer` around a draw returns close to the submission time, not the execution: the room's 24-step sunbeam march reads 2 ms from the timer and 140 ms from the frame delta at 960x540. GL runs the draw asynchronously on this driver and the query resolves before the work; per-stage GPU numbers on llvmpipe are only a lower bound. |
| R-26 | `RenderPipeline` volumetric fog | limitation | `setVolumetricFogDensity` is a uniform screen-space medium: it greys the frame evenly at any density and scatters no shadowed light, so sunbeams through a window cannot come from it | `--volumetric` left experimental, default 0 |
| R-24 | `ShaderEffect` GLSL samplers | note | A `sampler2D`/`samplerCube` without a precision qualifier returns `lowp`/`mediump` values on this ES driver: an RGBE exponent read as `t.a * 255` came back a fraction of an octave off | `precision highp sampler2D;` in the shader and the exponent code rounded |
| R-22 | `EnvironmentProcessor::generateIrradiance` | limitation | Monte-Carlo with the given sample count: 16 samples against a 400:1 environment produce texel-to-texel speckle that normal maps spread per pixel; 96 samples cost ~2 s per probe on this machine | Replaced by an exact CPU convolution over an 8×8-per-face downsample (~5 ms) |
| R-20 | `HeightFogPass` via `RenderPipeline` | limitation | The pass has `setColor`, the pipeline exposes only density/falloff/base height, so the fog keeps CNA's daylight haze colour | Fog density scaled by daylight, off at night |
| R-36 | `PbrEffect` environment lighting | limitation | One environment cube (and one irradiance cube) per draw and no per-pixel blend: a game with a grid of probes cannot fade between them across a surface, only split the surface and accept a step at every seam; the renderer generates the shader, so an SH ambient or a cube-array lookup cannot be added from outside | Nine probes in a grid, the floor in 3x3 sections each on its nearest probe; walls and ceiling whole (a step on plain plaster reads as an edge) |
| R-37 | `ContactShadowPass` | limitation | Runs only as a pipeline user pass (after the tonemap, on display values) or by hand; takes no normals, so surfaces facing away from the light shadow themselves within the thickness; rims every silhouette even with the light along the camera axis (the depth sampled across an edge falls in the bias..thickness band); output mirrored in V like every FullscreenPass draw (R-8) | `Render/ContactShadows`: a mask marched before the frame over a white source, gated by the prepass normals, multiplied in after the opaque pass; off by default |

## R-21 / R-23 Half-float IBL products

The magenta night ambient (audit round 3): with the pendant's shade at radiance 0.14 setting
the probe scale and the room's diffuse irradiance around 0.0004, a linear 8-bit irradiance cube
holds 0.7 codes of signal; each channel rounds to 0 or 1 independently, so a warm-lit wall
comes back as (1, 0, 1) codes. `--dump-probes DIR` writes the captured faces and the irradiance
strip per bake, and the per-probe "mean irradiance … chroma" log line shows the tint directly:
before (1.28, 0.90, 1.11), after the half-float path (1.20, 0.96, 0.80).

The route that works on EasyGL: `RenderTargetCube(HalfVector4)` (colour-renderable here), one
`SetRenderTarget(cube, face)` per face, a clip-space quad through `DrawIndexedPrimitives` with
a `ShaderEffect` that `texelFetch`es an RGBE-encoded RGBA8 `Texture2D` strip. `SpriteBatch`
into the face draws nothing (R-23), a `TextureCube` cannot take floats (R-21), and the cube's
`GetData` is `Color`-only, so the self-test reads the result back by sampling the cube from a
second shader into a `HalfVector4` `RenderTarget2D` (R-15). What would make this a one-liner
upstream: `TextureCube::SetData<HalfVector4>` for a `HalfVector4` cube, or an
`EnvironmentProcessor` option to emit float products.

## R-25 D3D-style projection on a GL clip volume

`PlanarReflection::prepare` first mapped the mirror plane to NDC z = 0 (where
`Matrix::CreatePerspectiveFieldOfView` sends its near plane) and the capture still showed the
exterior between the mirrored eye and the glass: GL clips at z = −1. A `glClipControl`-style
remap, or a z' = 2z − w in the vertex shaders, would let the whole depth range carry the scene.

## R-1 Degenerate-UV tangent fallback parallel to the normal

`modules/content/src/GltfImport/GltfImportCore.cpp`, `ComputeTangentsEXT`: when a primitive has
no `TANGENT` accessor the tangent is derived from the UV gradient per triangle. Palette-textured
meshes (every vertex at one texel, common in glTF-Transform exports and the Bitterli room
scenes) have zero UV gradients, so the accumulated tangent is NaN or zero and the finaliser
substitutes `(1, 0, 0)`. For any face whose normal is ±X the EasyGL PBR shader computes
`T = normalize(vTangent - N * dot(N, vTangent))` = `normalize(0)`, the TBN matrix is NaN,
`finalNormal` is NaN, `clamp(dot(N, V), 1e-4, 1)` collapses to the minimum and the Fresnel term
reaches 1: the surface reflects the whole environment (a black television screen read as light
grey; chrome rails on the sofa turned to noise).

Repro: `--view tv-close` before commit `b95ffa9` (screenshot `docs/archive/screenshots/m3-tv-close.png`
shows the fixed state). living-room-simulator's `ModelLibrary::repairTangents` counted 6 224 such tangents in
the current asset set.

Suggested fix upstream: choose the fallback perpendicular to the normal (cross with the least
aligned axis) and guard the shader's `normalize` with a length test.

## R-2 Cube readback fails once the cube was bound

`EasyGLTextureCubeRenderer::GetData` creates a framebuffer, attaches the face, and returns
`false` when `is_complete` fails; the shared `TextureCube::GetData` then throws
`NotSupportedException("...cannot read a cube face back to the CPU at the requested mip level")`.
A cube that was created, filled with `SetData` and immediately read works (first bake at load
time). The same cube, after it has been bound to a sampler unit by a draw and refilled with
`SetData`, fails. `EnvironmentProcessor::generateIrradiance` reads its input cube this way, so a
`SkySystem` that reuses its environment cube across bakes crashes the process on the first
rebake (`gdb` backtrace: `ReadCube` ← `generateIrradiance` ← `SkySystem::bakeEnvironment`).

Repro: `./build/bin/living-room-simulator --frames 60 --time 17:55 --day-length 1` on the revision before
commit "M5". Workaround: a fresh `TextureCube` per bake. Root cause not isolated (texture
completeness after a sampler bind? the still-bound sampler while attaching?); worth a CNA test
that binds, draws, refills and reads a cube.

## R-3 No back-face normal flip

`grep -rn gl_FrontFacing modules/renderers/easygl` finds nothing; `setDoubleSidedEXTProperty`
only selects `CullNone`. glTF §3.7.2.1 asks for the normal to be reversed on back faces.

## R-4 SSAO parameters in screen and far-plane fractions

Documented in the pass, but easy to miss: the defaults are calibrated for outdoor scenes with
tens of metres of depth. With `prepassFarPlane = 400` a living room showed 2 m halos. See
`RenderSettings::prepassFarPlane`.

## R-8 / R-9 Fullscreen pass orientation and double sRGB

Both were first found in cna-street; this project re-verified them on `1b3151f2f` and keeps the
same workarounds (`SkySystem` `uFlipV`, `SceneRenderer::applyMaterial` encode flag).

## R-10 Alpha-weighted specular in blended materials

Physically, a dielectric pane reflects `F(θ)` of the environment and transmits the rest; CNA's
non-premultiplied blend scales the reflection by the material alpha too. Drawing the pane
premultiplied with a black base colour gives `reflection + (1 − α) × background`, which is right
for clear glass; tinted glass would need `transmission × background`, which no fixed blend
state can express. A `TransmissionEXT` factor on `PbrEffect` (multiply the destination by a
colour) would cover it.

## R-14 Irradiance on the CPU

`generateIrradiance(16, 24)` and `generatePrefilteredSpecular(64, 5, 24)` read the source cube
back and integrate on the CPU; for three probes that is ~0.4 s per rebake on this machine and it
is what forced the incremental probe bake (one face per frame, products at the sixth). A GPU
convolution (the pipeline already has the fullscreen machinery) would make sky and probe
rebakes per frame affordable.

## R-36 One environment probe per draw

`PbrEffect` samples one prefiltered cube and one irradiance cube, chosen per draw, and its
program comes from the renderer's built-in effect family, so a game cannot add a second probe
sampler, a spherical-harmonics ambient or a cube-array lookup to blend probes per pixel. With
nine probes in the room the only gradient available is per item: the floor is cut into 3x3
sections that each take their nearest probe (`RoomScene::buildFloorAndCeiling`), and the
level steps at the seams; the ceiling and walls stay whole because the same step on plain
plaster reads as a rendering edge (plan.md §29, 2026-09-14 probes). An irradiance volume (a
3D texture or SH uniforms the fragment shader interpolates by world position) would give the
per-pixel gradient the section split approximates.

## R-37 ContactShadowPass rims silhouettes and shadows back faces

`ContactShadowPass::apply` marches the prepass depth from each pixel toward the light and
darkens where a nearer surface lies within `bias..thickness` of the ray. Run over a 1x1 white
source into a mask (there is no pipeline hook before the tonemap, and the scene target cannot
be read and rebound mid-frame), three things showed in the room: every surface facing away
from the light comes out shadowed, because the ray dips behind the surface's own depth within
the thickness and the pass has no normal to know it (living-room-simulator gates the mask by the prepass
normal's cosine to the light in its own composite); with the light direction set along the
camera axis so the ray climbs straight out of the depth, the mask still rims every silhouette
(the depth read across an edge mixes near and far and lands in the band), at bias 0.015 and
0.03 alike; and the mask arrives mirrored in V (R-8). The sanity probe that fixed the sign
conventions: light travelling away from the camera gives a white mask, toward it a grey one,
as documented.
