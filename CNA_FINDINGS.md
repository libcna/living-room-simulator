# CNA findings from cna-room

Bugs, surprising behaviours and limitations of CNA (`next` @ `1b3151f2f`, EasyGL renderer on
Mesa llvmpipe ES 3.2) met while building this project, with the evidence and the workaround
used here. Numbered so `plan.md` and commit messages can refer to them. "Bug" means the
behaviour contradicts CNA's own documentation or produces wrong pictures from valid input;
"limitation" means documented or defensible behaviour that still cost a workaround.

| # | Area | Kind | Summary | Workaround in cna-room |
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
| R-17 | `RenderPipeline` SSR | observation | With `setSSREnabled(true)` a rainy night view turned the white window frames pink/violet and the tree canopies into black noise (`docs/screenshots/m7-ssr-rain-night.png`); by day on the glossy TV screen it added nothing visible over the probe reflection | Left off (`--ssr` stays an opt-in) |
| R-18 | `RenderPipeline::getSceneTarget` | limitation | Returns null once `end()` has run, so an image-based exposure has to measure the scene before the transparent phase and post chain | Measured before `end()` |
| R-19 | `AutoExposureEXT` | works | Compute-shader log-average luminance reads back on llvmpipe (ES 3.2); the key value must be calibrated per scene (0.05 here: a lamp-lit room's log-average is ~0.0006 in scene units, a sunlit one ~0.075) | Analytic schedule as prior, measurement clamped to ×/÷1.8 of it |
| R-21 | `TextureCube` formats | limitation | `TextureCube` rejects `SurfaceFormat::ColorSrgbEXT`, uploads `SetData` as RGBA8 whatever the format, and `GetData` is `Color`-only, so CNA's IBL products are linear 8-bit under one shared `Intensity`: at night the room's diffuse irradiance is ~1/300 of the shade peak that sets the scale, below one code, and the rounding of R/G/B to 0 or 1 paints the ambient magenta (`docs/screenshots/m9-night-irradiance-8bit-vs-half.png`) | `FloatCubeUploader`: irradiance and prefiltered specular (one cube per roughness class, the material picks its class; the shader samples level 0 of a one-mip cube) convolved on the CPU, RGBE-encoded into a `Texture2D` strip and decoded by a draw into each face of a `HalfVector4` `RenderTargetCube` (self-tested by readback at start-up); the 8-bit path stays as the fallback |
| R-23 | `SpriteBatch` / `FullscreenPass` into a cube face | bug | With a `RenderTargetCube` face bound, `Clear` lands on the face but a `SpriteBatch` quad (and so `FullscreenPass::drawOverCurrentTarget`) draws nothing: the sprite projection is built from `GetCurrentRenderTarget2DSize`, which reports no target for a cube binding, so the quad is laid out for the back buffer | Face fills drawn as a clip-space quad through `DrawIndexedPrimitives` with a `ShaderEffect` |
| R-25 | EasyGL depth convention | limitation | XNA's projection matrices map the near plane to NDC z = 0 and EasyGL's shaders do `gl_Position = WVP * position` unchanged, so on GL (clip volume −1..1) the depth buffer only uses its upper half (a precision cost in large scenes) and clipping happens well inside the XNA near plane; a custom oblique near plane has to target −1, not the 0 the matrix suggests | Planar reflections build their oblique projection for NDC −1 |
| R-27 | `gltf_to_cnb` | limitation | Models with `KHR_materials_variants` (Khronos GlamVelvetSofa, SheenChair) are refused by the CNB v1 model schema (plan_cnb D9) while the runtime glTF import takes them | Those two stay on the glTF import; `scripts/compile-assets.sh` reports them |
| R-28 | `.cnb` texture references | note | A compiled model's textures are sibling assets resolved against the loading `ContentManager`'s root, not the `.cnb`'s directory: a `.cnb` in `assets/cnb/` loaded through the manager rooted at `assets/` fails on `<model>_tex0.png` | A second `ContentManager` rooted at `assets/cnb` for the compiled models |
| R-29 | `.cnb` model textures | limitation | A compiled model gives every part its own `Texture2D` for a shared sidecar image (the content manager's asset cache does not dedupe them), so a five-part sofa holds five copies of its palette texture: memory and any per-texture work (a mip rebuild) multiply with the part count | `ModelLibrary::remip` shares rebuilt textures by a content signature (117 copies shared across the room's models) |
| R-30 | `ShaderEffect` matrices | trap | `ShaderEffect::setWorldProperty` / `setViewProperty` / `setProjectionProperty` store the matrix and the draw call forwards it only to uniforms named `World` / `View` / `Projection`; a program that names them otherwise (CNAEXT's own `DepthNormalPrepass` effect reads `uWorld`) silently keeps its previous value, so a scene drawn through the prepass with `setWorldProperty` per object had every placed model at the origin, and the SSAO and any depth consumer read a wrong scene | `SetUniformMat4("uWorld", ...)` per object, as the shadow casters already did; a warning when a set matrix property matches no uniform would have caught it |
| R-31 | `RenderPipeline` colour grade | limitation | `RenderPipelineSettings` can enable the colour-grading pass and set its strength, but the pipeline exposes neither its `ColorGradePass` nor a way to hand it a LUT (`setLut` / `setVolumeLut` are on the pass, which is private), so through the pipeline the grade only ever copies its input | the room's white balance is a per-channel gain multiplied over the finished frame (the vignette pass); a `setColorGradeLut(Texture2D*)` on the pipeline would let the pass do its job |
| R-32 | `SsaoPass` depth bias and range | limitation | The occlusion test's depth bias (0.005) and range (a quarter of the screen radius, at least 0.01) are fixed fractions of the prepass far plane, with no setter: with a 40 m far plane an occluder must sit 0.2 to 0.4 m in front of the surface to count, so a sofa on a floor casts almost no ambient shadow (a 0.25/255 mean change over the frame at the default strength, 160 ms of llvmpipe for it); the same far plane feeds the depth of field, so the two effects pull it in opposite directions | the room's prepass far plane comes down to 16 m (`--prepass-far`), which brings the bias to 8 cm; a bias and range in world units on the pass would free the far plane for the lens |
| R-33 | `GraphicsDevice` sampler states with custom effects | limitation | `SamplerStates[n] = SamplerState::PointClamp` before a `DrawIndexedPrimitives` that draws with a `ShaderEffect` never reaches GL on EasyGL: the sampler trace (`CNA_EASYGL_SAMPLER_TRACE=1`) shows only the stock draws' Linear/Anisotropic states applied, so a custom pass samples every unit with whatever the last stock draw left there. The room's sunbeam march reads the packed RGBA8 prepass depth bilinearly as a result (a decode error at depth edges, tolerable for a march end); a pass that needs point sampling has no way to ask for it. Workaround: sample with `texelFetch` (integer texel coordinates bypass the sampler), which is what the room's sunbeam pass now does for both the depth and the atlas. |
| R-26 | `RenderPipeline` volumetric fog | limitation | `setVolumetricFogDensity` is a uniform screen-space medium: it greys the frame evenly at any density and scatters no shadowed light, so sunbeams through a window cannot come from it | `--volumetric` left experimental, default 0 |
| R-24 | `ShaderEffect` GLSL samplers | note | A `sampler2D`/`samplerCube` without a precision qualifier returns `lowp`/`mediump` values on this ES driver: an RGBE exponent read as `t.a * 255` came back a fraction of an octave off | `precision highp sampler2D;` in the shader and the exponent code rounded |
| R-22 | `EnvironmentProcessor::generateIrradiance` | limitation | Monte-Carlo with the given sample count: 16 samples against a 400:1 environment produce texel-to-texel speckle that normal maps spread per pixel; 96 samples cost ~2 s per probe on this machine | Replaced by an exact CPU convolution over an 8×8-per-face downsample (~5 ms) |
| R-20 | `HeightFogPass` via `RenderPipeline` | limitation | The pass has `setColor`, the pipeline exposes only density/falloff/base height, so the fog keeps CNA's daylight haze colour | Fog density scaled by daylight, off at night |

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

Repro: `--view tv-close` before commit `b95ffa9` (screenshot `docs/screenshots/m3-tv-close.png`
shows the fixed state). cna-room's `ModelLibrary::repairTangents` counted 6 224 such tangents in
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

Repro: `./build/bin/cna-room --frames 60 --time 17:55 --day-length 1` on the revision before
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
