# cna-room — engineering plan

A realistic, explorable 3D living room built on CNA (`next`), sharp-runtime (`next`) and the
EasyGL renderer. This file is the living plan for the whole project: it is updated every
iteration, and it is the first thing to read when resuming.

## CONTINUATION RULE

**Do not stop at feature completion.** Once the room renders, the job is to keep auditing,
measuring, improving realism, using more of CNA and CNAEXT where it genuinely helps, fixing
weaknesses and refining the scene until an external limit (usage budget, environment) prevents
further useful work. "The scene is good enough" is not a stopping condition.

Whenever work resumes:

1. read this section, then `NEXT.md` (the short continuation queue);
2. check `git status` / `git log` and the *Completed work* and *Measured results* sections below;
3. build (`cmake --build build --target cna-room`), run the canonical screenshot set
   (`scripts/capture-views.sh`), and look at the images before choosing a task;
4. pick the highest-value item: the largest realism defect first, then the largest technical
   weakness, then underused CNA functionality;
5. implement, build, run, inspect, measure, fix regressions, update this plan, commit;
6. repeat. When the backlog is empty, run the full audit list in §"Work when no obvious tasks
   remain" of the original brief (walk the room, close-ups, awkward angles, every weather state,
   every time of day, shadows, glass, exterior, materials, scale, profile, CNA/CNAEXT re-read,
   logs, licences, build reproducibility, architecture, resource lifetime, TODO/FIXME, placeholder
   assets, screenshot comparison) and generate a fresh ranked backlog.

Do not busy-loop. Every iteration must do real engineering work.

## 1. Project objective

Build the most visually realistic, technically sophisticated, polished 3D living room achievable
with CNA, as a serious showcase of CNA + CNAEXT + EasyGL + sharp-runtime. The user flies a camera
through a believable, lived-in residential living room with a convincing exterior seen through
its windows, a slow continuous day/night cycle and slowly evolving weather. No gameplay.

Quality bar: a viewer looking at a good screenshot should first notice the room, furniture,
lighting, weather and atmosphere, not primitive geometry or graphics-demo shortcuts.

## 2. Environment and constraints discovered (2026-09-12)

- **Machine:** 4 cores, 15 GB RAM, no GPU (`/dev/dri` absent). Rendering is Mesa 25.2.8
  llvmpipe under Xvfb: `OpenGL ES 3.2 Mesa`, MSAA 4x, MRT 4, anisotropic 16x, RGBA16F/RGBA32F
  render targets, compute (ES 3.1+) available. Frame times measured here are *software
  rasteriser* times; they are recordings, not budgets. GPU timer queries
  (`GL_EXT_disjoint_timer_query`) are present on this Mesa.
- **Network:** outbound egress is policy-restricted. Reachable: `github.com`,
  `raw.githubusercontent.com`, `media.githubusercontent.com`, `registry.npmjs.org`, `pypi.org`.
  **Blocked (403):** `api.github.com`, polyhaven.com, sketchfab.com, huggingface.co, ambientcg,
  kenney.nl, quaternius.com, wikimedia, archive.org, jsDelivr, and every other host probed.
  Consequence: third-party assets must come from GitHub repositories, npm or PyPI packages, and
  every licence must be verified from the repository's own metadata.
- **Toolchain:** GCC 13.3, CMake 3.28, Ninja, lld. SDL3 is built from CNA's vendored submodule
  (needs the X11/Wayland/audio dev packages listed in README).
- **Tools installed for the asset pipeline:** Node 22 (`@gltf-transform/cli`, `draco3dgltf`,
  `sharp`) and Python 3 (`Pillow`) in the scratchpad, used by `tools/` scripts.

## 3. Architectural overview

```
RoomApplication (Microsoft::Xna::Framework::Game)
 ├─ Camera / CameraController      free-flying camera, frame-rate independent
 ├─ RoomScene                       architecture + furniture placement (real-world metres)
 │    ├─ Architecture builder       walls, floor, ceiling, trim, window recesses, door (procedural)
 │    ├─ ModelLibrary               glTF/GLB (.cnb) hero furniture via CNA ContentManager
 │    └─ PropPlacer                 secondary objects, clutter, variation
 ├─ MaterialLibrary                 PbrMaterial per surface class, procedural texture baker
 ├─ SceneRenderer                   CNAEXT RenderPipeline, CSM, prepass, SSAO/SSR/bloom/tonemap
 │    ├─ Lighting                   sun/sky, lamps (punctual+probes), TV emissive, exterior lights
 │    ├─ SkySystem                  AtmosphericSky + stars/moon/clouds, IBL from the sky
 │    └─ Exterior                   buildings, street, trees, distant lights outside the windows
 ├─ TimeOfDay                       continuous solar clock, sun/moon position, colour
 ├─ Weather                         continuous state (cloud, rain, snow, wind, fog, storm)
 ├─ Particles                       rain / snow / hail (CNAEXT ParticleSystem)
 ├─ TelevisionContent               procedural screen imagery rendered to a RenderTarget2D
 └─ DebugOverlay / Profiler         stats, GPU timers, debug modes
```

Sources live under `src/` and `include/CnaRoom/` (CNA's module-layout validator on `next` keys
on `CNA_SOURCE_DIR`, so a consumer's own `src/` tree is fine).

## 4. Dependency strategy

- [x] CNA `next` as sibling `../cna`, consumed with `add_subdirectory`, `CNA_CNAEXT=ON`,
      renderer `OPENGLES3` (EasyGL reference identity; `OPENGL33` is the same implementation).
- [x] sharp-runtime `next` as `../sharp-runtime` (resolved by CNA), plus `Text.Json`/`Numerics`.
- [x] easy-gl / meta-gl `develop` as siblings (EasyGL's own dependencies).
- [x] SDL3, SDL3_image, SDL3_mixer, draco from CNA's submodules.
- [x] Pinned revisions recorded in `dependencies.lock`.
- [ ] `scripts/fetch-dependencies.sh` reproduces the sibling layout (written; verify on a clean tree).
- Rule: sibling repositories are treated as read-only. A CNA defect is worked around on this side
  and recorded in §"Discovered limitations" unless a local fix is genuinely impossible.

## 5. CNA API inventory (XNA layer, always compiled)

Verified in headers on `next` @ `1b3151f2f`:

- `Game`, `GraphicsDeviceManager`, `GameWindow`, `GameTime`, `ContentManager` (`Load<Model>`
  from `.gltf`/`.glb`/`.cnb`, `Load<Texture2D>`).
- `GraphicsDevice`: render targets, `GetBackBufferData` (screenshots), `SetVertexBuffer(s)`,
  `DrawIndexedPrimitives`, instanced draws, `SupportsCapability`, MSAA, sampler/blend/depth/
  rasterizer states.
- `PbrEffect` / `SkinnedPbrEffect`: metallic-roughness PBR with albedo/normal/metallic-roughness/
  emissive/occlusion/specular maps, texture transforms, alpha modes, double-sided, three
  directional lights, one `PunctualLightEXT` (point/spot with cube/2D shadow), `ImageBasedLightEXT`,
  `ShadowCascadeStateEXT`, sRGB flags (`setEncodeOutputToSrgbEXTProperty` must be **false** when
  drawing into the HDR scene target).
- `ShaderEffect`: custom GLSL (ES 3.00 floor) with uniforms/textures — for the sky, TV content,
  window glass, exterior details.
- `Model`/`ModelMesh`/`ModelMeshPart` (imported glTF: vertex/index buffers, bones, bounding
  sphere; no bounding box — computed on this side from vertex data).
- `Texture2D` (`CreateFromPixels`, `SaveAsPng`, `SetData`), `TextureCube`, `RenderTarget2D`,
  `RenderTargetCube`, `SpriteBatch`, `SpriteFont` (overlay), `BasicEffect` (debug).
- Input: `Keyboard`, `Mouse`.
- Front-face convention (XNA): a face is visible when its vertices are **clockwise seen from the
  front**, i.e. `cross(b-a, c-a)` points away from the viewer (cna-street finding CNA-F5).

## 6. CNAEXT API inventory (`CNA::Graphics`, `CNA_CNAEXT=ON`)

Verified in `modules/graphics-ext/include` and `docs/cnaext-engine-layer.md`:

| Subsystem | Type | Planned use |
|---|---|---|
| HDR pipeline | `RenderPipeline`, `RenderPipelineSettings` | RGBA16F scene target, ACES tonemap, exposure, gamma |
| Post | `BloomPass`, `FxaaPass`, `TonemapPass`, `ColorGradePass`, `FilmGrainPass`, `ChromaticAberrationPass`, `LensFlarePass`, `DepthOfFieldPass`, `MotionBlurPass` | subtle bloom, FXAA (+MSAA), subtle grade; DOF/aberration only for stills |
| Screen-space | `DepthNormalPrepass`, `SsaoPass`, `SsrPass`, `ContactShadowPass` | grounding contacts, glossy floor/TV reflections |
| Shadows | `ShadowMap`, `CascadedShadowMap`, `CubeShadowMap`, `SpotShadowMap` | sun through windows (CSM), lamp shadows (cube/spot) |
| Lights | `DirectionalLightEXT`, `PointLightEXT`, `SpotLightEXT`, `AreaLightEXT` (+`AreaLightBrdfTable`) | sun/moon, lamps, window as area light candidate |
| Indirect | `EnvironmentProcessor`, `Skybox`, `ImageBasedLightEXT`, `LightProbeVolumeEXT`, `LightProbeBaker` | sky IBL; probe grid for room bounce |
| Sky | `AtmosphericSky` (`radiance()` also on CPU), `AerialPerspectivePass` | physically based sky, sun colour, ambient |
| Volumetrics | `HeightFogPass`, `VolumetricFogPass`, `LightShaftPass` | exterior fog/haze under weather; shafts sparingly |
| Weather | `ParticleSystem` (GPU compute + instanced billboards) | rain, snow, hail |
| Many lights | `ClusteredLightSetEXT`, `ClusteredForwardEffect` | evaluate for exterior lights (no material textures) |
| Instancing | `InstancedRendererEXT`, `FrustumCullerEXT`, `LodGroupEXT`, `GpuInstanceCuller` | books, exterior windows/trees, clutter |
| Transparency | `TransparentDrawList`, `TransparencyMode`, `WeightedBlendedTransparency` | window glass, lamp shades, vase |
| Decals | `DecalPass` | wear/stains where plausible |
| Materials | `PbrMaterial`, `applyMaterial`, `materialFromGltfEXT`, `PbrMaterialExtensions` | material library |
| Debug/perf | `DebugDraw`, `DebugGizmos`, `GpuTimer`, pass timings | overlay, validation |
| Compute | `ComputeShader`, `StorageBuffer`, `AutoExposureEXT` | auto exposure for indoor/outdoor difference |
| Textures | `Texture2DArray`, `StorageTexture2D` | evaluate |

## 7. EasyGL capability inventory (measured here)

- `EasyGLRenderer initialized with OpenGL ES 3.2 Mesa 25.2.8`; MSAA up to 4x; MRT up to 4;
  indexed colour masks; anisotropic filtering up to 16x; texture formats Color, SNORM, 565/5551/
  4444, DXT1/3/5; render targets Color, RGBA16F, RGBA32F.
- Compute shaders (ES 3.1): expected available (runtime-probed by CNA); to be confirmed by the
  particle system's `usesCompute()` report.
- Custom `ShaderEffect` GLSL ES 3.00 executes (this is what all CNAEXT passes rely on).
- Known EasyGL-path costs (cna-street CNA-F19): the stock-effect draw rebinds every uniform and
  texture per draw; draw-call count is the lever.

## 8. Rendering architecture

Per frame:

1. **Update** — time of day, weather, camera, TV content, particles.
2. **Shadow pass** — `CascadedShadowMap` (2–3 cascades) for the sun/moon over room + exterior;
   casters culled per cascade. Lamp shadows via `CubeShadowMap`/`SpotShadowMap` for the one or
   two lamps that matter, updated only when needed (static scene ⇒ cache).
3. **Depth/normal prepass** — `DepthNormalPrepass` (MRT), roughness written for SSR.
4. **Scene** — `RenderPipeline::begin`: sky (AtmosphericSky drawn through the pipeline's sky
   hook or a custom pass), exterior geometry, room opaque geometry (`PbrEffect`), then the
   transparent phase (`TransparentDrawList`: glass, shades), particles outside the window.
5. **Post** — SSAO, SSR, contact shadows, bloom, tonemap (ACES), colour grade, FXAA; DOF only for
   stills.
6. **Overlay** — SpriteBatch debug text when enabled.

Colour workflow: linear. All albedo/emissive textures flagged sRGB, PBR output not sRGB-encoded
into the HDR target, tonemapper encodes for display.

## 9. Asset strategy

Given the network policy, sources are limited to GitHub / npm / PyPI. Verified candidates:

**KhronosGroup/glTF-Sample-Assets** (`raw.githubusercontent.com`, per-model `metadata.json`
carries the licence):

| Model | Licence | Use |
|---|---|---|
| SheenWoodLeatherSofa | CC-BY-4.0 (Darmstadt Graphics Group) | main sofa candidate |
| GlamVelvetSofa | CC-BY-4.0 (Wayfair) | alternative sofa |
| SheenChair | CC0 | armchair |
| ChairDamaskPurplegold | CC-BY-4.0 (Wayfair) | accent chair |
| StainedGlassLamp | CC-BY-4.0 (Wayfair) | table lamp (glTF-JPG-PNG variant) |
| IridescenceLamp | CC-BY-4.0 | lamp |
| AnisotropyBarnLamp | CC-BY-4.0 | lamp |
| GlassVaseFlowers | CC0 | vase with flowers |
| DiffuseTransmissionPlant | CC-BY-4.0 (DGG) | plant |
| IridescentDishWithOlives | CC-BY-4.0 (Wayfair) | bowl |
| DiffuseTransmissionTeacup | CC0 | cup |
| MandarinOrange | CC-BY-4.0 | fruit |
| BoomBox, WaterBottle, Lantern, Corset, ToyCar, Avocado, ClearcoatWicker | CC0 | small props |
| SpecularSilkPouf | CC-BY-4.0 | pouf |
| LightsPunctualLamp | CC-BY-4.0 | lamp |

Excluded: anything with a `LicenseRef-LegalMark-*` (logos), DamagedHelmet (NC).

**gkjohnson/3d-demo-data** (Draco + WebP glbs; each folder's README names source and licence):

| File | Author | Licence | Contents useful to a living room |
|---|---|---|---|
| bitterli-rendering-resources/white-room.glb | Jay-Artist | CC BY 3.0 | leather sofa, cushions, coffee table, TV, radiator, radio, books, magazine, candle holders, pictures, blinds, sockets, ceiling lamp, floor lamp, carpet |
| bitterli-rendering-resources/grey-and-white-room.glb | Wig42 | CC BY 3.0 | sofa, wood table, plants in pots, painting, mirror, bottle |
| bitterli-rendering-resources/bedroom.glb | SlykDrako | CC0 | wood furniture, lamps, curtains, vase, books, boxes, painting |
| bitterli-rendering-resources/little-lamp.glb | UP3D | CC0 | desk lamp |
| blendswap/dining-room.glb | MaTTeSr | CC BY 3.0 | books, bamboo planters |
| hdri/*.hdr | Poly Haven | CC0 | exterior IBL / night references (1k/2k) |

These scenes group geometry by material, so furniture is extracted by node-name prefix with
`tools/gltf-extract` (glTF-Transform: Draco decode, WebP→PNG, subtree select, recentre) into
per-object GLBs. Every extraction is recorded in `THIRD_PARTY_ASSETS.md`.

**@pmndrs/assets (npm, CC0)**: 18 Poly Haven HDRIs at 512² (EXR) — usable for IBL only.

**Procedural** (this repository): plaster, wood floor, carpet, fabric, paper, ceramic, metal
maps baked by `tools/bake` with seeds; architecture, trim, windows, door, radiator, outlets,
exterior buildings generated in code.

- [x] `assets/external/manifest.json` + `scripts/fetch-assets.sh` with SHA-256 verification (22 files).
- [x] `tools/gltf-extract` + `assets/external/extract-recipes.json` + `scripts/extract-assets.sh` (48 objects + 18 Khronos models).
- [x] Runtime import through `ContentManager::Load<Model>` on the extracted GLBs; textures re-uploaded with linear-light mip chains (works around GLTF-206 without a content build).
- [ ] `scripts/validate-assets.py` licence gate.
- [ ] Optional `.cnb` content build for faster start-up (currently ~8.5 s: 6 s procedural bake + 2.5 s import).

## 10. Licensing strategy

- Code: MIT (`LICENSE`).
- Assets: only CC0, CC-BY 3.0/4.0 (attribution honoured in `THIRD_PARTY_ASSETS.md` and README)
  or equally permissive. Unclear licence ⇒ not used. Required licence texts preserved beside the
  manifest. Never relicense third-party assets. Poly Haven HDRIs are CC0.

## 11. Scene composition (real-world metres, Y up, origin at floor centre)

- Room 6.2 m × 4.6 m, ceiling 2.7 m, wall thickness 0.30 m (exterior) / 0.12 m (interior).
- Long wall (−Z) with two windows (1.4 × 1.5 m, sill at 0.9 m) with 0.30 m recesses and sills;
  radiator under one window; curtains + blinds.
- Door (0.9 × 2.05 m) with frame and handle on the +X short wall; baseboards and wall trim.
- Focal group: sofa facing the TV wall (+Z), armchair at an angle, coffee table on a rug, side
  table with lamp, floor lamp behind the sofa, media console with TV, router, speakers.
- Bookcase on the −X wall with books (instanced, varied), boxes, baskets, framed photos.
- Plants near the window, painting and wall art, clock, ceiling fixture, outlets, switches.
- Clutter: remotes, magazines, cups, bowl, candles, blanket, cables (subtle).
- Outside: street/courtyard, neighbouring facades, trees, distant skyline, street lights.

## 12. Camera system

- Free-flying camera, position in metres, yaw/pitch, pitch clamped to ±89°, no roll.
- W/S/A/D move, arrow left/right yaw, arrow up/down pitch (mandatory), plus Q/E vertical,
  Shift sprint, Ctrl precision, mouse-look optional, R reset, canonical viewpoints on number keys.
- Frame-rate independent (delta seconds), smoothed acceleration.

## 13. Lighting strategy

- Sun/moon: one `DirectionalLightEXT` from `TimeOfDay`, colour/intensity from
  `AtmosphericSky::radiance` and an extinction model; shadows via CSM; shadow bias above one
  quantisation step (cna-street CNA-F9: the atlas is 8-bit on some paths).
- Sky: `AtmosphericSky` for the visible sky; IBL products regenerated from the current sky (and
  night sky) on a slow cadence (every N seconds of game time) via `EnvironmentProcessor`.
- Room bounce: three interior environment probes (`SceneRenderer::bakeInteriorProbes`): each
  renders the lit room into a half-float cube (8-bit sRGB fallback), two bounce iterations,
  irradiance from a gain-boosted copy (`probeBounceGain`), prefiltered specular from the raw
  capture; every item takes its nearest probe, exterior items keep the sky. Re-baked when lamps
  toggle (M5 will spread that over frames). `LightProbeVolumeEXT` remains a candidate for spatial
  variation if three probes prove too coarse.
- Lamps: `PunctualLightEXT` (one per draw — the most influential lamp per object), cube shadows
  for the key lamp, secondary lamps as per-object directional stand-ins, TV as emissive + a weak
  cool point light when on at night. Exterior lights at night as emissive + probes.
- Window as `AreaLightEXT` candidate for soft daylight on the floor (no shadows: evaluate).
- SSAO + contact shadows for grounding; occlusion maps on imported models.

## 14. Material strategy

Distinct classes: painted plaster, wood, varnished wood, fabric, leather, carpet, ceramic,
glass, brushed metal, polished metal, plastic (matte/glossy), paper, leaves, TV screen. Each a
`PbrMaterial` with physically plausible values; imported glTF materials via
`materialFromGltfEXT` with colour space audited. Subtle imperfections via roughness variation
maps; no exaggerated dirt.

## 15. Model-loading strategy

- Hero furniture: GLB → `cna_tool_gltf_to_cnb` → `.cnb` with mipmapped textures (per-map colour
  space), loaded by `ContentManager`. Runtime GLB loading as fallback for a tree without a
  content build.
- Bounding boxes computed from vertex data (`VertexBuffer::GetDataRawEXT`) for culling and
  fit-to-size placement; models scaled to their real-world size from a recorded `fitMetres`.
- Front-face convention: imported glTF (CCW) drawn with `CullClockwise` or winding flipped at
  extraction (cna-street CNA-F15) — to be verified on this CNA revision.

## 16. Texture strategy

Mip chains everywhere (`--mipmaps`), anisotropic 16x for floors/walls, sRGB for colour maps,
linear for data maps, texel density audited (≈ 512 px/m near the camera), no visible tiling.

## 17. Shadow strategy

CSM 3 cascades (High), split lambda tuned for a 6 m room + exterior, texel-snapped; slope-aware
bias; casters: architecture, furniture, lamps, plants, window frames, exterior; small clutter
excluded from casting when negligible. Lamp shadows only for the key lamp(s). Contact shadows
for the last centimetres.

## 18. Post-processing strategy

HDR RGBA16F, ACES, exposure from time of day (plus optional `AutoExposureEXT`), bloom threshold
above white with low intensity, FXAA (+ MSAA 4x if the pipeline target allows), subtle colour
grade, debanding dither; film grain/aberration off by default.

## 19. Indoor/outdoor rendering strategy

- Exterior items (`SceneItem::exterior`) take the sky IBL; interior items take their nearest
  interior probe. Exterior geometry is chunked (12 m) for the one-lamp-per-draw budget (§26).
- The window wall's reveals, sills and frames are real geometry; the panes are premultiplied
  reflective glass so the room reflects in them at night and the street shows through by day.
- The street is built in `Exterior.cpp` on a deterministic `Dice`, so the layout is stable
  across runs and screenshots stay comparable. Sun shadows from the terrace, trees and hedge
  fall on the road and lawn through the 60 m cascade range.
- Not done: parked cars and pedestrians (no redistributable assets reachable, see §2), fog /
  aerial perspective for the skyline (M6/M7), exterior wetness (M6).

## 20. Day/night cycle

Built (M5): `TimeOfDay` is a solar clock (latitude 50.1°, day 256 by default; `--time HH:MM`,
`--day-length MIN` real minutes per game day, default 24; `--latitude`, `--day-of-year`,
`--moon-age`, `--pause`; PageUp/PageDown ±30 min, P pauses, Home resets). Sun elevation and
azimuth from declination and hour angle (equation of time ignored); the moon lags the sun by its
age and takes its phase from it. `--sun` or the sun hotkeys freeze the clock and drive the sun
by hand. The sky's IBL products rebake when the sun moves > 0.035 rad (~2°, every ~8 s of real
time at the default speed). Lamps switch on below 1° and off above 5° elevation (hysteresis);
the television follows in auto mode; street lights share the switch for now. Exposure follows an
analytic schedule (`RoomApplication::scheduledExposure`: sky hemisphere average + 5 % of the
sun + moon + a lamp term), adapting in log space at 1.5/s; `--exposure` fixes it. Interior probes
re-capture one face per frame when the lamps change or the sun moved ~3°; the pendant's cube
shadow renders two faces per frame with per-face culling.

Not done: an image-based auto exposure (M7, `AutoExposureEXT`) — the analytic key over-exposes
a view that stares at the pendant; street lights on their own dusk schedule; lamps switching
with a short fade instead of instantly.

## 21. Weather system

Built (M6): `Sim/WeatherSystem` is a state machine over clear → cloudy → overcast → rain →
storm / hail (and snow below 1 °C) with dwell times of 0.3–1.5 game hours and a fixed transition
table; cloud coverage/density, haze, wind and precipitation glide towards each kind's targets at
rates per game hour, rain waits for the cover to arrive, wetness soaks in ~10 game minutes and
dries over an hour or two, snow settles while it snows and melts above 2 °C. Temperature is a
September model with a diurnal swing, colder under cloud (`--temperature` overrides; snow forces
−3 °C). Lightning is timed in real seconds during storms: a leader and a brighter return stroke,
fed to the sky shader (`uLightning`) and to `PbrEffect`'s third directional light from the
strike's azimuth. `--weather KIND`, `--hold-weather`, `--weather-speed`; F2 cycles kinds, F3
holds. Rendering: `Render/Precipitation` — 9 000 particles in a wrapping volume animated by a
custom `ShaderEffect` (camera-facing streaks for rain, wandering flakes for snow, hard dots for
hail), premultiplied over the HDR scene after the opaque pass with depth read, lit by a fraction
of the sky ambient (+ the street lights at night); the volume sits over the garden and street
while the camera is inside and around the camera outside. Surfaces: `RoomScene::applyWeather`
darkens and polishes the exterior materials by wetness, swaps their textures to a procedural
snow surface past a third of cover, puts a droplet normal map on the panes while it rains and
rocks the curtain panels about their rods with the wind.

Not done: trees keep their leaves in snow; the hedge's sides whiten with its top; no puddles or
planar reflections on the road (SSR is optional and screen-space); rain is not lit per street
lamp (one radiance for the whole volume); no thunder (no audio in scope); precipitation does
not fall inside the room's volume when the camera is outside looking in through the window
(the volume follows the camera then, which includes the room: accepted).

## 22. Performance strategy

Metrics: CPU frame, per-stage CPU, GPU timers per pass, draw calls, shadow draws, triangles,
visible objects, particle counts, RT memory estimate, load time. Techniques: frustum culling,
material sorting, instancing (books, exterior), reduced shadow casters, cached lamp shadows,
LOD for exterior, mip chains.

## 23. Quality targets

- Real room look at 1280×720 and 1920×1080 stills; no floating objects, no wrong scale.
- Night genuinely dark, lit by lamps/TV; stars/moon visible in clear weather.
- Every weather state visibly different; transitions smooth.
- Software rasteriser: keep a frame under ~2 s at 1280×720 so iteration stays practical; on a
  real GPU the design targets 60 fps.

## 24. Validation strategy

- `--frames N --screenshot PATH` headless runs under Xvfb after every change.
- Canonical viewpoints (`--view NAME`) with deterministic `--time`/`--weather` overrides:
  entrance overview, sofa→TV, TV→sofa, bookshelf, window day, window night, material close-up,
  wide corner.
- `scripts/capture-views.sh` renders the set; `tools/compare-images` reports per-view diffs.
- Unit tests (GTest via CNA's vendored googletest) for time-of-day, weather evolution, camera,
  geometry builders.
- `git diff --check`, warnings as errors for project code.

## 25. Planned milestones

- [x] M0 Bootstrap: CMake, minimal Game, headless screenshot (`ba1ed9e`).
- [x] M1 Camera + room architecture + procedural PBR materials + HDR pipeline + sky + CSM + prepass/SSAO.
- [x] M2 Asset pipeline: manifest + fetch + glTF-Transform extraction + runtime import with mip rebuild; 41 placements of 40 objects (`3fa4dc3`→ this commit).
- [x] M3 Lighting and shadows: sun CSM, sky IBL, interior probes (half-float capture), lamps with a cube shadow, night sky calibration.
- [x] M4 Exterior through the windows: street, terrace opposite, neighbours, trees, hedges, street lights, skyline; glass as reflection + transmission.
- [x] M5 Time of day: solar clock, lamps at dusk/dawn with hysteresis, exposure schedule, probe and lamp-shadow work spread over frames.
- [x] M6 Weather: state machine, rain/snow/hail particles, wet and snow-covered surfaces, droplets on the panes, lightning, curtain sway.
- [x] M7 Modern rendering: image-based exposure within the analytic prior, television picture (render target), on-screen overlay, exposure-relative bloom, daylight height fog; SSR evaluated and left off; contact shadows not integrated (no pipeline hook).
- [x] M8 Performance pass: front-to-back opaque order, size-aware shadow caster culling, cheaper IBL products, sRGB tables, PNG texture cache; measured on this VM.
- [ ] M9 Realism polishing + audits, repeated.

## 26. Discovered limitations

CNA-specific findings are catalogued with evidence and workarounds in `CNA_FINDINGS.md`
(R-1 … R-32); this section keeps the project-level notes.

- Sun shadows stop at -1° elevation (`drawShadows` returns when the light comes from below)
  and the moon takes over when it is up; between the two there is a frame where shadows vanish
  rather than fade. The sun colour is already zero there, so it is invisible in practice.
- Exposure: the analytic schedule is the prior and the centre-weighted, highlight-rejecting
  compute meter moves it within ×/÷2.5 (M9); a view taken beneath the lit pendant still shows
  the globe as a white disc, which is the shade's own radiance rather than the meter.
- Timings are not comparable across sessions: the M4 binary rebuilt and run on the 2026-09-13
  machine measures the same opaque-pass cost as the current one (GPU opaque 623 vs 609 ms at
  1280×720), i.e. this VM's llvmpipe is ~10× slower on that pass than the one M1–M4 were
  measured on. Compare only within a session.

- CNA `SsaoPass`: `radius` is a **fraction of the screen** (UV units) and its depth bias/range are
  fractions of the prepass far plane (0.005 / max(radius/4, 0.01)). With a 400 m far plane a
  6 m room got 2 m biases and streaky halos; the prepass and `setCamera` now use a 40 m far
  plane (`prepassFarPlane`) and radius 0.03. Passes that read the prepass (SSR, DOF, fog) share it.
- CNA glTF import: a material without `occlusionTexture` leaves the occlusion slot empty; binding
  the packed metallic-roughness map there (red channel empty in glTF-Transform palettes) blacks
  out every ambient term. `Material::ormHasOcclusion` guards it (cna-street saw the same).
- CNA glTF import: `KHR_materials_transmission` becomes alpha = 1 - factor, so glass with factor
  1 vanishes; imported blend materials get an alpha floor of 0.30 and the TV screen is overridden.
- CNA glTF import: `metallicFactor` defaults to 1 (glTF spec) and Bitterli scene palettes carry
  near-mirror roughness (0.04) — leather/lacquer overrides in `RoomScene` (roughness 0.55/0.35).
- Imported textures arrive with one level (GLTF-206): every unique texture is read back and
  re-uploaded with a mip chain at load (`ModelLibrary::remip`), ~2 s for the current set.

- CNA glTF import (`ComputeTangentsEXT`): a triangle with degenerate UVs (palette-textured
  meshes map every vertex to one texel) gets the fallback tangent (1, 0, 0); on a face whose
  normal is ±X the shader's Gram-Schmidt step normalises a zero vector, the NaN normal reaches
  `clamp(dot(N,V))`, and the surface becomes a full-strength mirror of the environment (the TV
  screen read as light grey, the sofa's chrome rails as chaos). `ModelLibrary::repairTangents`
  reads every imported vertex buffer back, replaces tangents that are NaN or parallel to the
  normal (6.2 k across the current set) and re-uploads. Worth reporting upstream: the fallback
  should be chosen perpendicular to the normal, and the shader should guard `normalize`.
- CNA EasyGL `PbrEffect`: double-sided materials are drawn with `CullNone` but the shader never
  flips the normal for back faces (no `gl_FrontFacing` anywhere in the renderer), so the back of
  a leaf or curtain is lit as if it faced the other way. Kept; noted for M9.
- CNA `EnvironmentProcessor::generateBrdfLut` stores scale/bias in an 8-bit texture; measured
  (N·V 0.5, r 0.5) = 0.71/0.016 — bias quantised to 1/255, acceptable.
- CNA `Texture2D`/`RenderTarget2D` half-float readback works on llvmpipe (`GetData(HalfVector4*)`),
  detected at run time (`detectCaptureFormat`) with the 8-bit path kept as fallback.
- CNA `PbrEffect`: one punctual light per draw. Exterior geometry is therefore built in 12 m
  chunks (`ChunkSet` in `Exterior.cpp`) so each chunk's nearest street lamp is the right one; a
  wide pavement mesh would otherwise be lit by a single lamp or none. Buildings, roofs, frames,
  glass, trees and posts are chunked the same way (387 items, ~117 visible through a window).
- Global tonemapping vs. a lamp-lit room and a lamp-lit street: physically the street (3-30 lux)
  is 30-100x darker than the room (100 lux) and ACES at one exposure renders it black, whereas
  the eye adapts locally. Two documented cheats: street luminaires at 3x their real lumens and an
  "urban sky glow" horizon term ~50x a real city's 0.5 cd/m^2 in `SkySystem` (night only).
  Local tone mapping / auto exposure (M7) may allow retiring them.
- Masked (alpha-tested) foliage: box-filtered alpha mips change coverage with distance (a far
  canopy turned into a solid ball). `TextureBaker::upload` rescales each level's alpha so the
  fraction above the cutoff matches level 0 (alpha coverage preservation); `MaterialLibrary`
  turns it on for any surface with alpha below 128. CNA's own mip generation has no such option.
- Glass: `BlendState::NonPremultiplied` weights the whole PBR output by alpha, so a 4 %-alpha
  pane loses 96 % of its reflection. Panes are drawn with `BlendState::AlphaBlend` (source ONE)
  and a black base colour: the Fresnel reflection is added and (1 - alpha) of the background
  shows through (`Material::reflectiveBlend`). Correct for clear glass; a tinted pane would need
  the transmission colour multiplied in, which this blend cannot express.
- Shadow casters are not alpha-tested by `CascadedShadowMap`'s caster effect: tree canopies cast
  the shadow of their full spheres (dense enough to pass at this stage).
- Network: see §2. No Poly Haven / ambientCG scans; surfaces are procedural or from glTF assets.
- No GPU: all timings are llvmpipe.
- CNA: `PbrEffect` carries one punctual light per draw (documented budget).
- CNA: `RenderPipelineSettings::setVolumetricFogDensity` is a uniform screen-space medium: it
  greys the frame evenly and produces no shadowed scattering, so no sunbeams through the window
  (`--volumetric D` stays an experimental opt-in, default 0).
- CNA: `TextureCube` is 8-bit only (no float upload, no sRGB, `Color` readback), and the shared
  `Intensity` of `ImageBasedLightEXT` ties the irradiance scale to the prefiltered cube's
  peak. Irradiance now bypasses it (a `HalfVector4` `RenderTargetCube` filled by a decode draw,
  `FloatCubeUploader`); the prefiltered specular follows as one half-float cube per roughness
  class (a mip chain cannot be written through a cube render target).
- CNA: `AtmosphericSky` drawn via `FullscreenPass` rendered vertically mirrored in cna-street's
  CNA (CNA-F7) — verify on this revision before relying on it.
- CNA: imported glTF textures may arrive without mip chains (cna-street GLTF-206) — the content
  build compiles referenced images with `--mipmaps` under their full names.

## 27. Technical debt

- Shadow pass: casters are culled per cascade against the slice's light-space box and a
  minimum size of six atlas texels (395 draws for the entrance view, 129-145 for views through
  the window); 12 m exterior chunks are still coarse against a 5 m first cascade, and
  books/chunks are not instanced (`InstancedRendererEXT` unused).
- Opaque pass on llvmpipe is fragment-bound (~600 ms GPU at 1280×720 on this VM): the PBR shader
  with three directional lights, a punctual light, IBL and PCF is what it is; on a GPU it is a
  few milliseconds. Resolution and `--no-ssao` are the levers here.
- Start-up: 21.9 s warm at 1024² (materials 6.5 s from the PNG cache, models ~8 s of glTF
  import, probes 2.3 s). The glTF import could go through CNA's `.cnb` content build.
- Probe bake at start is 2.3 s (detection + 3 probes × 2 iterations × 6 faces at 64 px);
  re-bakes are spread one face per frame. The mapping detection (4 sky captures) could be cached.
- The green accent wall and plants tint the probe irradiance noticeably (teal ceiling in
  `m3-entrance`); check the bounce gain (1.5) against a reference once the exterior lands.
- Lamp glows are flat emissive rectangles/spheres; the pendant reads as a hard white disc at
  night exposure. Needs bloom tuning or a soft falloff texture (M7).
- Start-up 8.5 s (procedural bake 6 s at 1024², import 2.5 s): cache baked textures / content build.
- SSAO noise visible as speckle on bright glossy surfaces; compose blur is 5×5 box.

- Sky IBL rebake is synchronous on the CPU (48² cube, 16/24-sample products); fine for a
  still sun, needs spreading across frames once the sun moves continuously (M5).
- The pendant shade shows a streaky halo (bloom/SSAO interaction) — investigate in M7.
- Sun colour from the transmittance model is on the warm side at 38° elevation; calibrate
  against reference photographs when the exterior lands.

## 28. Future improvement queue

See `NEXT.md` for the ordered queue. Audit log (what the contact sheets showed and what was done):

- 2026-09-13 audit round 1 (`scripts/capture-views.sh --quick`, 26 views): (1) night interiors
  washed out by bloom halos — the exposure-relative threshold at 1.6 let every shade bloom into
  a blob: threshold 3.0, intensity 0.12, four iterations; (2) evening (17:45) interior yellow and
  dusk (18:20) pink everywhere — the single-scattering sky turns the whole dome orange at low
  sun: chroma blended towards twilight blue away from the sun below 10° elevation, keeping the
  model's luminance (`twilightChroma` in `SkySystem`, shader and CPU); (3) daytime ceiling
  teal from the accent wall's bounce: probe bounce gain 1.5 → 1.25; (4) crash at 18:20 with
  the moon down (unfitted cascades read by the receiver): fixed; (5) the `material` viewpoint
  showed a radiator: repointed at the table props; (6) `window` viewpoint sat under the pendant:
  moved back and lower. Round 2 re-renders of the affected views confirmed 1-3.
- 2026-09-13 audit round 2 (clutter and lighting): night interiors showed red/green speckle on
  every wall — the probes' irradiance was 16-sample Monte-Carlo against a 400:1 environment
  (the pendant shade), quantised into an 8-bit cube scaled by that shade. Fixed by dimming
  emissive glows ×0.12 during probe captures (the punctual lights carry the lamps' energy) and
  integrating the irradiance exactly on the CPU from the half-float capture (`integrateIrradiance`,
  8×8-per-face downsample, sRGB storage attempted and refused by `TextureCube`: R-21/R-22).
  Placed the remaining assets: avocado and a teacup, a low cabinet between the windows with a
  candlestick, a dish of olives and a leaning picture, a landscape over the pedestal table, three
  dark canvases by the chest, a mirror beside the fireplace, a lantern on the hearth and a second
  floor lamp by the door (its bulb joins the lamp switch as a 350 lm reading lamp). The silk pouf
  loads at last (the extractor downgrades `KHR_materials_specular` from required to used).
- 2026-09-13 audit round 3 (26 views on the round-2 binary, `screenshots/audit/`): (1) every
  night interior carried a magenta cast on surfaces lit by the probes only (sofa fronts, coffee
  table sides, floor). `--no-probes` removed it; `--dump-probes` and the new per-probe "mean
  irradiance / chroma" log line put the tint in the irradiance cube itself: linear 8-bit at the
  shade's scale holds 0.7 codes of a night room's irradiance, and R/G/B round to 0 or 1
  independently (chroma 1.28/0.90/1.11). Fixed with `FloatCubeUploader`: the CPU convolution
  goes RGBE-encoded through a `Texture2D` strip and a clip-space quad into a `HalfVector4`
  `RenderTargetCube` (self-tested at start-up), for the probes and the sky alike; chroma after
  1.20/0.96/0.80, daylight unchanged (mean pixel 65/73/73 before and after). Two CNA notes on
  the way: `SpriteBatch` into a cube face draws nothing (R-23) and samplers need `highp` (R-24).
  (2) The `window-close` viewpoint had ended up inside the floor lamp's shade: moved.
  (3) Dusk (18:20) exterior facades saturate pink-orange under the low sun; acceptable for a
  sunset but worth a look at the sun colour curve. (4) Shade radiances recalibrated from lumens
  over shade area (pendant 0.06, floor lamp 0.10, table lamp 0.12, sconces 0.25): the pendant
  still clips to white at night exposure, as a real paper globe does, but no longer floods the
  frame.
- 2026-09-13 audit round 4 (26 views on the planar-reflection build, `screenshots/audit4/`):
  the mirror shows the bookcase by day and the television screen the windows and the sofa
  (`day-tv-close`); the window panes carry the lamps and the television at night. Defects:
  (1) `window-close` turned the wrong way (yaw sign): now -25°; (2) the reading lamp's bare
  bulb (radiance 1.2) blooms into a disc that swallows the lamp: 0.6; (3) the exposure meter
  (compute-based, centre-weighted, highlight-rejecting) reads a lamp-lit room ~3x brighter than
  the analytic schedule; the band widened to 2.5x lifts the night interiors from exposure 51 to
  ~117 (walls read, the globe clips white with a halo) — judged in the next round across all
  night views; (4) fixed-frame captures now settle their exposure (the audit's six frames had
  been showing a half-adapted value).
- 2026-09-13 audit round 5 (26 views at the 2.5x exposure band, `screenshots/audit5/`): the
  night interiors now read as lit rooms (walls, ceiling, furniture visible; exposure ~117),
  the pane reflections carry the pendant, shades and bulb in the street views, the reading
  lamp's bulb no longer floods. Defects: (1) the pendant globe blooms into a halo that fills the
  frame in the views beneath it: bloom intensity 0.12 -> 0.08; (2) dusk (18:20) interior walls
  and frame go pink-mauve where the lamps' 2700 K meets the twilight sky's chroma: the twilight
  target moved from (0.62, 0.72, 1.0) to (0.60, 0.78, 1.0); (3) no direct sun ever reached the
  room by day: the street trees' full-sphere canopy shadows covered the windows (fixed with
  sparse leaf-cluster shadow proxies, `SceneItem::shadowOnly`; the first version, four
  clusters per blob, was as opaque as the blobs; seven per tree at ~half the silhouette lets
  dapples onto the curtains and reveals at 13:00). The "no exterior shadows" reading was a
  misread of `--shadow-tint`, which colours the cascades, not the shade; the caster culling
  changes nothing (`--no-shadow-cull`, per-cascade caster counts in the log). Two clear-sky
  midday views (`sun-window`, `sun-corner`) join the audit set.
- 2026-09-13 audit round 6 (28 views, `screenshots/audit6/`, sheet in
  `docs/screenshots/m9-audit-round6.png`): the dapples show on the curtain in `sun-window`, the
  lit facade windows vary, the dusk pink is softer, the night interiors read. Across the sheet
  the lens look was a touch heavy (every corner dark, grain aliasing in the thumbnails): vignette
  0.18 -> 0.12, grain 0.025 -> 0.015. The pendant's halo in `night-bookshelf` stays (a view
  taken beneath a lit globe). No new geometry or lighting defects found.
- 2026-09-13 audit round 7 (28 views, `screenshots/audit7/`, after the 1024 px texture cap,
  the shared rebuilt textures, the specular classes and the eased lens look): side by side with
  round 6 at 960x540 the close-ups (`material`, `tv-close`) show no softening from the cap, the
  glossy night surfaces look the same with the class cubes, the lens look sits better. One
  regression: the Khronos lamp by the hearth had lost its bulb in `night-sofa-to-tv`. Cause:
  the texture content signature (size plus two 16 x 16 blocks) matched its emissive map,
  black but for the bulb, with another black-cornered 2048^2 map loaded earlier (and the
  lantern's, the boom box's and the water bottle's emissives with each other), so the lamp
  glowed with a stranger's texture. The signature now also folds sixteen full rows and
  sixteen full columns (a simulation over the sidecar PNGs found no collision left; 156 ms
  of GPU reads for the whole set); the bulb is back. Two older defects seen at full size:
  (2) the large picture on the pier between the windows hung edge-on (its image faces +X
  natively, like `picture-medium`; yaw -90 turns it into the room), which had read as a dark
  blade from the ceiling in every window view and, from the entrance, as a dark shape on the
  wall beside the window that a first reading took for a tree shadow; (3) the paper globe of
  the pendant is a blended material in its source scene, so the picture on the far wall showed
  through it in the evening views: it draws opaque now (depth written again, still no shadow
  of its own bulb); (4) the direct sun under cloud kept a floor of 8 % and 24 % of its power
  under an overcast deck, so grey days still cast crisp shadows on the street: the direct
  share is now the chance of a gap (`(1 - coverage)^1.25`) plus a thin-cloud term, ~90 % clear,
  ~50 % cloudy, ~5 % overcast, ~3 % snow, ~1 % rain, none in a storm; what passes through
  cloud widens the receivers' PCF radius (`setShadowFilterRadiusEXT`, capped at 2 texels by
  CNA), and a key light under a tenth of the ambient skips the cascade pass altogether.
- 2026-09-13 audit round 8 (28 views, `screenshots/audit8/`, after the round 7 fixes): the
  large picture now hangs on the pier facing the room (a dark landscape with balloons, seen in
  every window view), the pendant's globe is a solid paper ball by day and evening, the lamp by
  the hearth has its bulb, the snowy and overcast street casts no crisp shadows. No new defects
  at thumbnail or full size.
- 2026-09-13 audit round 9 (28 views, `screenshots/audit9/`, with the depth of field and the
  corrected prepass): the SSAO now sits under the furniture instead of around phantoms; the
  wide interiors are barely touched by the lens at f/4; the close-ups gain a soft foreground.
  Defect: every street view had focused on the window mullion at the frame's centre (0.77 m)
  and blurred the street. The autofocus area grew to 128 x 80 px and its depths are split at
  their largest gap into a near and a far cluster: a near cluster filling under 60 % of the area
  is an obstruction and the far cluster's median takes the focus (street 17 m, street-left 6.9
  m with the picture beside the window soft, material 2.2 m, tv-close 1.4 m, lamp 4.0 m,
  entrance 6.0 m). The frame log now carries the focus.
- 2026-09-13 audit round 10 (28 views, `screenshots/audit10/`, with the cluster autofocus): the
  street views are sharp on the street again, the picture beside the window goes soft in
  `street-left` as a lens would have it, the close-ups keep their subject. No new defects.
- 2026-09-14 audit round 11 (28 views, `screenshots/audit11/`, with the hedge fringe and the wet
  street): the hedges read as bushes, the rainy street views carry the sky in the pavement,
  nothing regressed. The white balance landed after this round (round 12 will show it).
- 2026-09-14 audit round 12 (28 views, `screenshots/audit12/`, with the white balance): the
  overcast and cloudy interiors read a shade warmer and cleaner, the lamp-lit views keep their
  warmth, the dusk street less magenta. No new defects.
- 2026-09-14 audit round 13 (28 views, `screenshots/audit13/`, with the road out from under the
  grass, the puddles and the 16 m prepass plane): the street views show asphalt, kerbs and
  paint between the pavements for the first time; the rainy ones a wet road with the sky and
  the lamps in its puddles; the interiors as in round 12. Logged below at full size.
- 2026-09-14 audit round 14 (28 views, `screenshots/audit14/`, with the window interiors):
  every view without a facade in it is byte-identical to round 13 (the renders are
  deterministic; the palette whites at 0.84 landed after the binary this round copied), and
  the street views differ only in the windows. By day the rooms behind the glass are dark and
  the curtains barely there, as from a street; at night a lit window read as a dark rectangle
  with a bright dot, because one image served as albedo and emissive and the daytime albedo
  has to stay dark. Fixed below with a separate night bake; the whites, the television clear
  of the curtains and the glow go to round 15.
- 2026-09-14 audit round 15 (29 views, `screenshots/audit15/`, with the palette whites at
  0.84, the window glow bake and a new `sun-morning-corner` view at 10:30 with the sun clear
  of the trees): the sofa, chairs and table keep their shading by day and no longer clip;
  the lit facade windows read as warm rooms with their lamps and curtains from every street
  view, day and night; the morning sun lays the window's pattern over the floor and the
  wall behind the sofa. Every view differs from round 14 by a mean of 1-4 levels (the
  whites are in most frames, and the exposure adapts to them). Seen in the round and fixed
  after it: the three black panels by the chest of drawers (the bedroom's palette
  paintings, no image to show) are now baked canvases, and the street trees' leaf blobs
  each carried their own highlight, so their normals now bend toward the crown's.
- 2026-09-14 the sun had never been in the room. Building the sunbeam pass (below) meant
  probing the cascade atlas point by point, and every ray through a window came back
  shadowed: the exterior in M4 had given our own house a front skin, one thin box across the
  whole facade a millimetre outside the wall, windows included. From inside nothing showed,
  but as a shadow caster it kept every ray of direct sun out, and what the sun views had
  shown as sun on the floor and "dapples" since M4 was leakage through the wall's base
  (the depth bias is a quarter of a metre along the light, the wall is 0.45 m thick along
  it) and along the tree proxies' edges. The skin is now pieces around the openings, and the
  morning and midday sun lay the windows' pattern across the floor and the sofa for the first
  time; the trees' shadow proxies still dapple it. Every earlier note on the sun views should
  be read with this in mind. Diagnostics kept: `CNA_ROOM_SUNBEAM_POINTS=x,y,z;...` logs the
  atlas compare at world points, `CNA_ROOM_DUMP_ATLAS=file.png` writes the atlas after the
  shadow pass, `CNA_ROOM_NO_TREE_SHADOWS=1` leaves the crowns' proxies out.
- 2026-09-14 audit round 16 (29 views, `screenshots/audit16/`, with the front skin cut, the
  sunbeams, the canvases, the tree crowns and the window glow): the three sun views carry
  direct sun for the first time, the morning one laying the left window's dappled pattern
  over the floor, the table and the sofa's arm, the midday ones lighting the sheer and the
  sills; the beams read as a soft haze under the windows and are plainest looking toward
  them. Every other view is within a few levels of round 15 (the trees' crowns and the
  canvases account for the day and bookshelf differences); no regressions. The beam pass
  costs about 140 ms at 960x540 and 210 ms at 1280x720 by frame delta; neither the CPU
  stage nor the GPU timer attributes it (GL runs the draw asynchronously and the timer query
  returns the submission), so the frame log's "beams" numbers are not the cost.
- 2026-09-14 audit round 17 (29 views, `screenshots/audit17/`, with the sheers no longer
  casting, the dust motes, the wall clock and the beam pass reading by texel): every view is
  within 0.3 levels of round 16. The clock reads on the -X wall in the bookshelf and
  entrance views; the motes are specks inside the beams at this size; nothing regressed.
  The stove's fire and the candle came after this round's binary and go to round 18.
- 2026-09-14 audit round 18 (29 views, `screenshots/audit18/`, with the stove's fire, the
  candle and the street facades' recesses): the night views carry the fire in the stove's
  arch (the sofa view shows the embers and a flame, the entrance view its glow at the frame's
  edge) and the candle's flame on the low cabinet, the night exposure adapting by a tenth of
  a stop; the street views show the windows in their recesses with sills and downpipes.
  The day interiors are unchanged. No regressions. The street furniture came after this
  round's binary.
- 2026-09-14 audit round 19 (29 views, `screenshots/audit19/`, with the street furniture and
  the first parked cars): the street views carry the bench, the bin, the bicycle and the
  cars (the red one's roof over the hedge, the blue one through the left window), every
  other view within a level of round 18. No regressions. The lofted bodies, the shop front,
  the snow on the props and the motes' gate came after this round's binary.
- 2026-09-14 audit round 20 (29 views, `screenshots/audit20/`, with the lofted car bodies, the
  shop front, the snow on the props and the motes' gate): the per-view differences against
  round 19 sit in the five street views only (the shop's lit glass in the yellow building,
  the cars' chamfered sills and sloped bonnets, the snow-topped red car in `snow-day-street`),
  every interior view within a level of round 19. No regressions. The sun views' frame
  times in the log (2.5 s for `sun-corner`) are contention: the half-size beam renders
  ran alongside this round. The half-size march came after this round's binary.
- 2026-09-14 audit round 21 (29 views, `screenshots/audit21/`, with the half-size beam
  march, the television's light following its picture and the steam over the cup): every
  view within a fifth of a level of round 20 in the mean, the largest `sun-morning-corner`
  at 0.16 (the beams' upsample), so none of the three moves the canonical views (the steam
  is a close-up detail, the set's light a few levels on the mantel under the lamps). No
  regressions. The wall lights and the window lights came after this round's binary.
- 2026-09-14 audit round 22 (29 views, `screenshots/audit22/`, with the drum wall lights and
  the windows as lights): the sconces read as fittings in `day-tv-close` and glow as drums
  at night; the day views sit 2-5 levels from round 21 in the mean (the window lights and
  the exposure answering them), the door wall brighter in `day-corner`. One defect: the
  night views were 10 levels darker (`night-street` 74.8 to 64.5 in the mean), because the
  night sky's glow through the windows, small as it is, still made a lamp against the
  pendant and moved the exposure. The window lights now scale with the sky's daylight, so
  the night calibration stands. The handles, the picture's level and the props came after
  this round's binary.
- 2026-09-14 audit round 23 (29 views, `screenshots/audit23/`, with the window lights scaled
  by the daylight, the handles, the picture's dark-room level, the throw, the book, the post
  and keys, and the chimney smoke): `night-street` back within a level of round 21 (the
  night exposure restored), every other view within two levels of round 22 in the mean; the
  throw reads as cream knit over the sofa's back in `day-corner` and `day-tv-to-sofa`, the
  drum sconces glow as rounded shades at night, the smoke stays above the street views'
  top edge under snow (its tail shows in `snow-day-street` only as a faint smudge). No
  regressions. The frame times sit where round 21 left them (the plumes are two draws).
- 2026-09-14 audit round 24 (29 views, `screenshots/audit24/`, with the hall beyond the door
  ajar, the night-drive programme and its fades, the smoke band in the simulation layer):
  the night interiors moved four to five levels in the mean, and the cause was a defect,
  not the hall: the programme now fades up from black at each start and the captures'
  sixth frame sits a tenth of a second in, so every canonical night view showed the set at
  a fifth of its picture. The schedule is offset ten seconds, so second zero lands
  mid-landscape (`mod(t + 10, 70)`). The hall's light shows through the gap in
  `night-entrance` and `night-bookshelf`; the street views sit within a level. The signage,
  the facade extras and the passing clouds came after this round's binary.
- 2026-09-14 lens flare (the pipeline's `LensFlarePass`, wired as `--flare I[,T]` with the
  threshold divided by the exposure like bloom's): at 0.05 the lamps throw small cyan ghosts
  across the frame's centre, tasteful in the wide night views, but a view beneath the pendant
  fills its middle with large yellow discs and the sun views gain nothing. Off by default;
  the option stays for stills that want it.
- The pendant's halo, tried once more: `--bloom-iterations 2/3/4` on `night-bookshelf` barely
  moves it, because the disc is the shade itself at seven times white under the camera, not
  the bloom; only the exposure or the shade's radiance would, and both are calibrated. Left as
  it is, with the options kept for the next audit.
- Still open from the audits: the interior is a little flat under overcast light (SSAO only,
  no directional bounce); the TV wall's sconces read as plain boxes in daylight; dusk facade
  saturation; the pendant's halo in the views taken beneath it.

## 29. Completed work

- 2026-09-14 M9 render smoke test: `ctest` gains `render-smoke`, a headless run of the binary
  (three frames at 320x180 with 64 px textures under `xvfb-run`) whose screenshot must exist
  and not be black; it skips (exit 77) where `xvfb-run` is missing. Eighteen seconds on this
  machine; it catches a binary that no longer starts or draws.
- 2026-09-14 M9 a shop front: the terrace building the bicycle leans on has a shop on its
  ground floor, one wide window in a dark painted frame with two mullions across the bays
  beside its door, lit at night like the lit rooms, under a fascia board proud of the front
  over door and window alike.
- 2026-09-14 M9 lofted cars: the box bodies became hulls lofted through five cross-sections
  (a sill that tucks in, the waist at its widest, a shoulder, a top sloping to the nose and
  a little to the tail) with fanned nose and tail caps; the profile now reads as a hatchback
  over the hedge rather than a crate.
- 2026-09-14 M9 motes gated on the sun: the specks' glint now fades with the key light's
  luminance against the ambient's (none below 2.5 times the ambient, full at 7.5), so a
  cloud-dimmed sun lights the air but not the motes, which had read as snowflakes across a
  cloudy room; their default size is 2 px at 540 lines. The README's hero image retaken.
- 2026-09-14 M9 snow on the props: the cars, the bench, the bin, the gutters and the bicycle's
  tyres join the weather's material list (their tops take the snow, the paints go matte when
  wet), with an exterior black rubber and a painted bench wood of their own so the snow does
  not reach the wall clock's case or the terrace's doors, which had shared those materials.
- 2026-09-14 M9 parked cars: three hatchbacks from boxes and quads (a body, a waistline, a
  trapezoid of tinted glass over a painted roof, wheels and chrome caps, bumpers, lamps,
  mirrors and a grille), two across the road and one at our kerb whose roof shows over the
  hedge, in blue, silver and red metallic paints. The paints keep a roughness of 0.4: at
  0.26 a bonnet seen from the room at a grazing angle mirrored the whole overcast sky and
  read as a white panel on a red car; some of that sky still sits on the flat tops, as it
  does on a real car.
- 2026-09-14 M9 street furniture: a slatted bench on cast ends, a litter bin with its lid ring
  and hood, and a bicycle (a diamond frame of turned cylinders, torus rims with eight spokes,
  saddle, stem and bars) leaning on the terrace opposite. They went on the near pavement
  first and vanished: the hedge hides that pavement from every window, so what the room can
  see of the street is the road and the far side. A `tube(a, b, r)` helper turns a Y-cylinder
  onto a segment with `Matrix::CreateFromAxisAngle`.
- 2026-09-14 M9 facade detail: every street building's front is now a 0.2 m skin in pieces
  around its openings (piers, spandrels, lintels per bay and storey; the door bay open to
  its lintel) over a body that stops a reveal's depth behind, so the windows and doors sit in
  real recesses whose reveals take the sun's shadow, with the frames and the glass at the
  back of the recess and a concrete sill through it; a concrete plinth band at the foot of
  each front; a gutter along each eave (or the parapet's foot) with a downpipe and its swan
  neck down one end in a dark painted metal. Sixty thousand triangles in the street view
  against forty before, no measurable cost on the frame.
- 2026-09-14 M9 the candle: the candlestick on the low cabinet was the steel alone; it now
  holds a wax candle (a cylinder in the cup) with two crossed flame cards of their own
  material (a smaller, brighter card) and a 12 lm lamp, lit with the room's lamps and
  flickered gently by the same update as the stove's fire; the flame's glow catches the rim
  of the cup.
- 2026-09-14 M9 the stove's fire: on cool evenings (lamps on, the weather under 14 C) the wood
  stove burns: an ember bed on the grate (`TextureBaker::embers`, charcoal lumps with glowing
  cracks between them and a dull red where the bed is hot, the image doubling as the emissive
  map so only the cracks glow), three crossed flame cards above it (`TextureBaker::flame`, a
  teardrop with noise-torn edges, pale core to red rim, blended by its alpha and emissive
  only), and a 260 lm orange lamp in the firebox. `RoomScene::update` ramps the fire over
  seconds and flickers it at three unrelated rates: the cards breathe in height and lean,
  their emissive and the embers' follow, the lamp with them. Found on the way: the weather
  model's diurnal temperature had its sign inverted (coldest at 15:00, warmest at 03:00);
  it now peaks at 15:00, so a clear night sits near 13 C and the stove is lit.
- 2026-09-14 M9 wall clock: a clock on the -X wall beside the bookcase whose hands follow the
  scene's time (`RoomScene::setClockTime`, fed from the day clock every frame): a shallow black
  case (a cylinder turned onto the wall), a cream dial baked by `TextureBaker::clockDial`
  (twelve bars, sixty ticks, a dark bevelled rim, alpha 0 outside the disc so the quad is a
  masked circle), and two black hands built pointing at 12 and turned about the dial's
  normal by the negative angle, each a few millimetres off the face. The bakers test
  checks the dial's shape and marks. The first placing landed inside the bookcase (the
  layout's -X wall carries the bookcase from z -0.3 to 1.5); the clock sits at z 1.95.
- 2026-09-14 M9 dust motes: specks drifting in the room's air (`--motes N[,PX]`, 400 of a
  1200 buffer), drawn additively after the beams. Each is a camera-facing quad a fixed few
  pixels across (a mote is a point of glare to a camera whatever its distance, so the offset
  is applied in clip space), its centre wandering slowly through the room box (convection,
  nothing falls), lit by the same atlas compare as the air in the vertex shader and collapsed
  where the key light does not reach, so they show only inside the beams; their glint is
  the key light under the beams' forward phase. Depth-tested against the scene, so the
  furniture hides them. A GLSL note: `half` is a reserved word in ES 3.00.
- 2026-09-14 M9 sunbeams: sunlight scattered by the room's air, as a fullscreen pass of its own
  (`Sunbeams`, drawn additively over the opaque HDR scene before the pipeline's post chain).
  Each pixel's ray, from the prepass depth and the inverse view-projection, is clipped to the
  room's box and marched in 24 jittered steps; each step samples the cascade atlas the
  receivers use (the same cascade matrices and compare) for lit or unlit air, the lit steps add
  in-scattered key light under a Henyey-Greenstein phase (g 0.75, so the beam glows most
  when the view looks toward the window) with a plain exponential transmittance, and the
  medium is confined to the room, the sky's haze answering for the outside. `--sunbeams
  D[,G,N]` (default 0.12 per metre); `CNA_ROOM_DEBUG_SUNBEAMS=1..6` paints the decoded depth,
  the world position, the shadow at the surface, the atlas, the raw scatter and the ray's
  clip. The pipeline's own `VolumetricFogPass` could not be used: the pipeline never hands it
  a shadow map (R-26) and its chain is private. 214 ms at 1280x720 on llvmpipe (a half-size
  march would cut it); the pass is measured by the exposure meter like any light. The depth
  and the atlas are read with `texelFetch`: a custom-effect draw keeps the last stock draw's
  sampler state on each unit (R-33), and neither may be filtered.
- 2026-09-14 M9 half-size sunbeam march: the march now runs into a half-size target of its
  own (HalfVector4 where the device renders to it, else Color) and is added over the scene
  through a bilinear upsample; the beams are low frequency, so the two frames differ by a
  level or so at most (a per-pixel compare of the window view at 960x540: mean 0.2, peak 13
  of 255, no halos at the window frames). The march happens before the pipeline opens its
  scene target: re-binding that target afterwards discards it (the pipeline's usage), which
  the first attempt did and lost the opaque scene, leaving only the transparents over black.
  At 960x540 the window view costs 850 ms with the beams off, 856 with the half-size march
  and 939 marching every pixel, so the beams are now within the frame's noise. The debug
  views (`CNA_ROOM_DEBUG_SUNBEAMS`) stay full size over the scene; `--sunbeams-full` marches
  every pixel.
- 2026-09-14 M9 the television's glow follows its programme: `TelevisionContent` renders the
  same shader a second time at 32x18 after each picture and reads it back (2 KB a frame; the
  shader is smooth at that scale, so the mean is the picture's), decoded from the sRGB the
  picture is written in. `RoomScene::updateTelevisionGlow` gives the "television" lamp the
  mean's colour at the luminance of its calibrated cool white, and its level the mean's
  luminance over the programme's own mean (measured over the 70 s cycle with
  `CNA_ROOM_TV_TIME_SCALE`, which runs the programme faster than a capture's 1/60 s frames:
  the sunset landscape sits at 0.33-0.34 and warm, the studio at 0.19 and blue, the test card
  at 0.30), so the calibrated lumens stay the long-run level and the cuts and pans move the room's light
  around it (`CNA_ROOM_DEBUG_TV` logs the mean each frame). The programme then gained a
  night drive (a dark road, street lights sweeping past, oncoming headlights, the dashboard's
  glow: 0.034) and each programme fades up from black over half a second, the schedule now
  30 s landscape, 15 s night drive, 20 s studio, 5 s test card; the cycle's mean is 0.23 and
  the room's light from the set swings from a tenth (the cuts) through 0.15 (the drive) to
  1.5 (the landscape). With the
  lamps on the effect is a few levels on the mantel; with the lamps off (`--lamps off --tv
  on`) the set is the room's light and its colour and level change with the programme.
- 2026-09-14 M9 steam over the cup: a plume of 24 soft billboards (`Steam`, a static
  buffer animated in the vertex shader like the motes and the rain) born on the rim of the
  cup-and-saucer on the coffee table (the placed model's bounds: its top, over its centre),
  rising 22 cm over about 2.4 s while they sway, lean with the convection, grow from 1.6 to
  8 cm and thin; the fragment shader cuts each puff into wisps with a three-octave noise that
  drifts up through it. Lit as a white scattering medium by the irradiance at the cup (the
  nearest probe's mean, E / pi in scene units, now kept per probe as `meanIrradiance`), so
  the plume sits in the exposure by day and under the lamps alike. Drawn last among the
  transparents with depth read and premultiplied alpha, skipped in reflection captures;
  `--no-steam`.
- 2026-09-14 M9 wall lights: the two sconces flanking the television were the White Room's
  lattice sconce, a pure-white translucent panel in a chrome grid that read as a flat white
  box by day in every audit (an off-white tint on its panel moved it a few levels, no more).
  They are now built: a brass back plate and arm (`sconce_brass`, metallic), a linen drum
  shade 15 cm across and 16 cm tall open at both ends (`sconce_linen`, double-sided, lit as
  fabric by day and glowing 0.16 with the lamps, the 300 lm over the drum's area) and a
  frosted bulb inside seen through the ends (`sconce_bulb`, a bare bulb's 1.2 when lit); the
  lamps' point lights moved to the bulbs. The fittings cast shadows. The `wall-sconce`
  extract recipe and its asset row are gone.
- 2026-09-14 M9 the windows as lights: the three probes hold the sky's light through the
  windows as one level each, so a surface by the glass was lit no more than the far wall and
  the room read flat under overcast skies (open since round 13). Each window now carries a
  wide spot lamp at its centre (`Lamp::daylightPortal`; a Lambertian opening's hemisphere as
  a cone full to 52 degrees and gone at 89), its colour the sky's chroma and its intensity
  the sky's hemisphere-average radiance over the opening's area, refreshed from the sky each
  frame (`RoomScene::updateWindowLights`, `CNA_ROOM_WINDOW_LIGHT` scales it, 0 off); off at
  night with the sky. Left out of the probe captures, which see the sky themselves. The
  effect's 1 / (1 + d^2) keeps the near field finite, and the probes carry the window's
  light too, so this over-counts near the glass: the price of a gradient the probes cannot
  hold. Under cloudy noon the door wall, which faces the windows across the room from a dim
  probe, gains 40 % against the exterior; the sofa's back and the floor by the windows
  4-5 % (the soft sun's patch already lights them), the pier between the windows nothing
  (grazing). Two findings on the way: the lamp cache in `applyLamp` re-applied a lamp only
  when its index changed, so a level that moved between frames (the fire's flicker, the
  windows) never reached the effect; it is reset each frame and across captures now. And a
  point light 8 cm off a wall lights that wall only next to itself (grazing incidence), so
  the window wall stays as the probes paint it.
- 2026-09-14 M9 window handles and the picture's level: each casement carries a brass
  espagnolette handle (a square rose, a neck and a lever hanging closed) on its stile by
  the mullion, room side, so the windows read as openable in the close views. The
  television's picture drops from 200 to 120 cd/m^2 (a set's dark-room mode; its lamp from
  250 to 150 lm to match): at 200 the picture paled under the tonemapper next to the
  lamp-lit walls, and the ACES curve desaturated it.
- 2026-09-14 M9 lived-in props: a knitted throw draped over the left end of the sofa's back
  (a strip of quads down the front, over the top and down the back from the placed model's
  back bounds, with folds across its width that deepen down the hangs and a wavy hem;
  double-sided, in a new `TextureBaker::knit` rib pattern, cream wool), a paperback left open
  face up on the magazine (cloth covers under two page blocks), and on the chest of drawers'
  free end three envelopes and a bunch of keys on a ring (a torus and two keys in polished
  metal), placed from the chest's bounds. The knit's first bake came back in the old ochre
  after its recolour: the bake cache keys on name, seed and size, not on the baker's
  parameters, so `kCacheVersion` is 7 now (the rule: bump it whenever a bake changes).
- 2026-09-14 M9 chimney smoke: the plume drawer (`Steam`) takes its rise, life, sizes and a
  drift as parameters now, and a second population of 64 puffs serves the chimneys across
  the street. `RoomScene::applyWeather` lights two hearths in three (their pots recorded by
  the exterior builder, `chimneyTops_`) when the weather is cold, fully below 8 C and gone
  above 14 (the band that lights our own stove), each plume born on a 0.3 m disc, rising
  3.2 m over 7 s while the puffs grow from a third of a metre to 1.2 m at full opacity and
  lean with the wind at half its speed (at the wind's own speed a 10 s plume streaked 40 m
  and read as scattered wisps). Lit as grey soot: six tenths of the sky's ambient plus a
  little sun, so the smoke sits a shade darker than an overcast sky and warms under a low
  sun; a faint smudge in a still, as thin smoke is, plainest against the snow sky. The
  chimneys sit at the top edge of the canonical street views, so the smoke shows in them
  only as its leaning tail.
- 2026-09-14 M9 the door ajar and the hall beyond: the door leaf and its handle now swing 32
  degrees into the hall about the hinge at the far jamb, and a hall stands beyond the door
  wall (`RoomScene::buildHallway`): 1.2 m wide along the wall, its own oak floor, plaster
  walls, ceiling and skirting, a frosted dome light that stays lit whatever the room's lamps
  do (a lamp of 600 lm that never dims, `hall`, and a constant glow on `hall_dome`: a hall
  without a window keeps its light on), a navy wool coat on a brass peg and a pair of shoes
  by the far wall, both on the side the gap looks at. The views past the door show another
  lit space now instead of a flat white panel.
- 2026-09-14 M9 street signage: the shop on the terrace opposite carries a sign box on its
  fascia (a lettered board baked by `TextureBaker::signboard`, a word of dark blocks with
  notches for eyes and crossbars, mapped once across the face; its lettering in the glow,
  0.25 with the street lights, about 2000 cd/m^2 for the box's cream), and a bus stop stands
  on the far pavement by the kerb: a pole on a base plate, a yellow flag in a metal rim at
  the top, a timetable case at eye height whose panel lights with the street (0.12). Both
  are chunked with the street furniture and cast shadows.
- 2026-09-14 M9 passing clouds: the sun through the deck was one steady average
  (`cloudDim`, the gap share plus what thin cloud passes) however broken the sky. Now
  `cloudBehindSun(coverage, seconds)` (simulation layer, tested) says whether the sun sits in
  a gap or behind a cloud at this moment, a two-tone drift of 47 and 113 s periods against
  the coverage with a soft edge, so under a broken sky the sun swaps between the gap's
  brightness (the gap share over the open share, capped at the open sun) and the thin
  cloud's smear over a minute or so, the shadows sharpening in the gaps and softening
  behind; the average over time stays `cloudDim`, a clear sky never hides the sun, an
  overcast one always does, and second zero (every capture) sits at the typical state, so
  the canonical views hold. The daylight in the room now breathes as clouds pass.
- 2026-09-14 M9 facade extras: by a hash of each window's place (the builder's dice would
  reshuffle every building after an extra draw), 28 % of the upper windows across the street
  carry a window box (a terracotta trough proud of the sill, a leafy top in the hedge's
  material, four to thirteen red and yellow blooms) and the next 8 % a satellite dish (a
  sphere pressed flat, tilted up and toward the street, on a bracket with its arm ahead);
  every other chimney has a television aerial (a mast and three crossbars in the gutters'
  metal). All chunked with the street and casting shadows.
- 2026-09-14 M9 running raindrops: the droplet normal map is a flipbook of eight frames now,
  the same drop set at eight phases with the running drops (15 % of them, stretched) slid
  down the tile by their own speed (one or two whole tiles a loop, wrapping, so the loop
  is seamless; a baker test holds a whole loop to the first frame and half a loop to a
  moved minority) and a thin fading trail above each; the glass takes the frame at six a second
  while it rains, so the runners slide a 0.4 m tile in about 1.3 s. Stills are as they were
  (frame zero at second zero); in motion the rain runs down the panes.
- 2026-09-14 M9 lamp haze: the beam march carries a second term for the shadowed lamp (the
  pendant): at each step the lamp's light at the point (the effect's own 1 / (1 + d^2)),
  its visibility from the lamp's cube shadow map (the light-to-point direction against the
  stored distance over the range, the same compare as the receivers'), a Henyey-Greenstein
  phase at g 0.4 against the light's travel, and an in-scatter coefficient of its own
  (`--lamp-haze D`, default 0.6; the extinction stays the air's), so the night views gain a
  soft halo about the globe and a faint lift under it. The pass now runs on the haze alone
  when no key light is cast (the moon down), the cascade part dropped. The march runs at
  half size as before. Two findings on the way: `--haze` already named the sky's haze, so
  the option is `--lamp-haze`; and at the 0.05 first tried the term was invisible (the
  lamp's radiant intensity is 0.005 in scene units, so the coefficient has to sit near one
  to read next to walls at a few ten-thousandths).
- 2026-09-14 hygiene: the chimney-smoke band is `chimneySmokeLevel(temperatureC)` in the
  simulation layer, with a sim test (nothing at 14 C, all at 8 C, half at 11 C, monotone).
- 2026-09-14 M9 tree crowns: the canopies were nine leaf spheres each shaded on its own, so a
  tree read as a cluster of balls with a highlight apiece. The blobs' normals now bend 0.7
  toward the direction from the crown's centre (squashed 1.4 in y so the underside reads as
  beneath), and the canopy shades as one volume, lit on top and dark below, under sun and
  under an overcast sky alike.
- 2026-09-14 M9 canvases: the three "paintings" by the chest of drawers were the bedroom's
  palette-textured panels, which have no image to show and rendered as three black
  rectangles on the wall in every view of that corner. They are replaced by three built
  canvases (0.40 x 0.55 m on 25 mm stretchers, a row over the chest): `TextureBaker::canvas`
  bakes an abstract colour-field painting per kind (soft-edged rectangles of muted colour
  brushed over a ground, the cloth's weave under the paint, matte ORM), spanning a quad a
  hair in front of each stretcher (`addQuadUv`); raw cloth wraps the edges. The triptych
  recipe and manifest row are gone. The bakers test checks the three grounds.
- 2026-09-14 M9 window glow: the lit facade windows carry two bakes, the daytime albedo (a
  dark room behind its curtains) and a night emissive (`windowInterior(..., glow = true)`:
  the room lifted to a tenth of the shade or so, the lamp's halo doubled, the curtains lit to
  0.5, the television's wash doubled), uploaded as the material's emissive map. A lit window
  now reads from the street as a warm rectangle with its lamp and curtain folds instead of a
  dot in a dark one. The bakers test covers the night kinds.
- 2026-09-14 M9 exposure compensation: `--ev STOPS` multiplies the adapted exposure by
  2^stops (a camera's compensation dial, clamped to ±3; the overlay shows it). An EV sheet
  (entrance 12:00 cloudy at 0/+0.4/+0.8, 22:00 clear at 0/+0.4, overcast +0.4) read +0.4 as
  brighter and plausible by day, +0.8 as bright, and +0.4 by night as an exaggerated pendant,
  so the default stays 0 and the dial is there for a viewer who wants the daylight lifted. The
  window-interior baker keeps the television clear of the curtains (a set half hidden by
  cloth read as a defect), the bakers test covers the four kinds (lit brighter than dark,
  drawn curtains warm, television cooler than the dark room with the same curtains, glass
  roughness in the ORM), and the bake cache version went to 6.
- 2026-09-14 M9 palette whites: the White Room's sofa, armchairs and coffee table carry
  palette texels near 1.0 and clipped to featureless slabs under the window; their palette
  parts now sit at 0.84 of themselves (white paint ~0.85, white cloth ~0.75), which keeps a
  little shading in the seat cushions and the table top by day.
- 2026-09-14 M9 window interiors: the facade windows had been flat rectangles (black by day,
  a uniform warm, dim or cool glow at night). `TextureBaker::windowInterior` bakes what a
  window shows, spanning it once (`MeshBuilder::addQuadUv`): a dark room with a brighter
  ceiling, cream curtains in folds at the sides (drawn across for the dim kind, lit from
  behind), a lampshade and its glow for the lit kind, a television's cool rectangle and wash
  for the cool kind; the albedo doubles as the emissive map, so the night glow carries the same
  picture. The bake cache version went to 5.
- 2026-09-14 M9 puddles and the road: the wet-street reflection gathers into puddles (a
  two-octave value noise over the ground; the low spots mirror, the film between reflects a
  quarter as much and is broken up by the asphalt's normal map; asphalt 0.85 at 1.8 m cells,
  paving 0.6 at 0.9 m). Chasing why the road showed none of it (`CNA_ROOM_DEBUG_PUDDLES=1|2`
  paints the mask, then the overlay's world position, over the surfaces) turned up two things:
  an overlay drawn over triangles another shader just wrote can land a few depth quanta behind
  them and fail the equal test (the pavement passed, the road did not), so the overlay steps a
  centimetre toward the camera; and the road itself had never been visible: the distant grass
  plane 2 cm under the pavements lay 10 cm above the road's trench, so every street view since
  M4 had shown grass between the kerbs (the road paint, the wet gloss and now the reflections
  all landed under it). The plane leaves the trench out now; the road, its lines and the lit
  windows mirrored in its puddles show from the pavement and from the windows.
- 2026-09-14 M9 SSAO bias: with the prepass right for the furniture, four SSAO settings on the
  overcast entrance told apart by nothing (a 0.25/255 mean change against no SSAO at all, for
  160 ms of llvmpipe): CNA's occlusion test biases and ranges by fixed fractions of the prepass
  far plane (R-32), 0.2..0.4 m at 40 m. The prepass far plane is 16 m now (`--prepass-far`;
  the depth of field and the autofocus clamp there, which the lens cannot tell from 20), the
  radius 0.04 and the strength 1.0: twice the effect, still gentle.
- 2026-09-14 M9 white balance: a camera-like partial balance against the light that carries
  the room: the sky's (its ambient with a share of the sun's and the moon's) until its level
  falls under ~0.003 at dusk, the lamps' 2700 K from there when they are on. Its chromaticity
  is taken out by `--white-balance` (0.6) scaled 1.25x under the sky and 0.5x under the lamps,
  as a per-channel gain with the brightest channel held at 1, multiplied over the finished
  frame by the vignette pass (the pipeline's colour grade has no LUT access, R-31). An overcast
  interior loses its blue-grey cast, the dusk street part of its magenta, a lamp-lit room stays
  warm (a first version balanced against `daylight` alone and cooled the lamp-lit room too far,
  then weighed the moon so heavily that night balanced against it and warmed instead).
- 2026-09-14 M9 wet street: a third reflection plane just above the pavements (`exteriorOnly`:
  the street and sky, none of the room, and nothing that tops out under the plane, or the
  mirrored camera under the road looks up at the lawn's underside; enabled by the scene only
  while the street is wet and not under snow) mirrors the houses, lamps and sky into the
  asphalt and paving, added over their plain draw (`Material::reflectionOverlay`) by a Fresnel
  share from F0 0.02 times the wetness (`reflectionTint`), rippled by the asphalt's normal map.
  Two things the wall planes never met: a quad that fills the frame has no corner inside it
  (the visibility test is its corners' box against the frustum now), and the oblique remap
  keeps the side of the plane holding the far corner it picks, the ground's side for a street
  seen from above, so `prepare` probes a point on the camera's side and hands the plane over
  the other way round when it comes out clipped (`tests/ReflectionTests.cpp` covers the case).
  From the windows the hedge hides most of the road; the garden path and the far pavement
  carry the lit windows and the lamps.
- 2026-09-13 M9 hedge fringe: the front-garden hedges were clipped boxes with a crisp
  silhouette; masked leaf clumps (`hedge_fringe`, the hedge's own leaf bake at 70 % coverage,
  double-sided) now straddle the top edges and the cut ends, tracked by the weather like the
  hedge itself (a snow-dusted variant swaps in past a third of cover).
- 2026-09-13 M9 depth of field: the pipeline's `DepthOfFieldPass` (a thin lens on a 24 mm
  sensor, 35 mm f/4 by default, `--dof F,MM`, `--no-dof`) over the depth/normal prepass, which now
  also runs for it alone; centre-area autofocus reads a 128 x 80 block of the prepass depth back
  (packed RGBA8 or half-float, decoded on the CPU), splits it at the largest depth gap into a
  near and a far cluster (a near cluster under 60 % of the area is an obstruction: a cable, a
  lamp stem, the window mullion in front of the street) and pulls the focus at 6 per second
  (settled within a frame in captures); `--focus D` fixes it; the overlay shows lens and focus.
  Cost on llvmpipe at 960 x 540: about +90 ms of post. The autofocus's first readings were
  nonsense (0.46 m at a far wall) and `--dump-depth` showed why: the prepass had been drawing
  every placed model at the origin since M7, because `ShaderEffect::setWorldProperty` only
  feeds a uniform named `World` and the prepass program reads `uWorld` (R-30); with the world
  set by name the depth and normals are right, so the SSAO on the furniture is right for the
  first time too.
- 2026-09-13 M9 content load: `scripts/compile-assets.sh` (CNA's `gltf_to_cnb`, sidecar PNGs
  kept beside the `.cnb`, a second `ContentManager` rooted at `assets/cnb`), `ModelLibrary`
  prefers an up-to-date `.cnb` and falls back to the import; the prepare step turned out to be
  the cost (mip rebuild readbacks and tangent repair): rebuilt textures shared by a content
  signature (a compiled model hands each part its own copy) and imported textures halved above
  1024 px (`--model-texture-size`), per-model load/prepare/readback/mip times in the log.
  Then `Image::halved` rewritten (direct indexing instead of the wrapping `at()` per tap,
  rows split over up to four threads for levels of 256^2 and up; output identical to the old
  code bit for bit): a 1024^2 chain 35 -> 6-9 ms, the mip build of the load 5.6 -> 2.4 s,
  the content load 17.5 -> 14.9 s. The signature grew rows and columns after a collision
  (audit round 7); its GPU reads cost 0.16 s of the load.
- 2026-09-13 M9 specular classes: the prefiltered specular joins the irradiance in half-float:
  `prefilterSpecular` (GGX, Hammersley, N = V = R, bilinear `sampleCube` over the float faces)
  into five 32 px cubes per probe and per sky bake at roughness 0.06/0.28/0.50/0.72/0.94; a
  material binds the class nearest its roughness × the mean of its roughness map
  (`Material::roughnessMapMean`: baked surfaces average the ORM, imported parts sample the map
  at their own vertices' UVs, so a palette part reads its texel) with `PrefilteredMipCount` 1;
  `--no-specular-classes` keeps CNA's 8-bit chain; `tests/CubeTests.cpp`. The A/B at night
  (`material`, `tv-close`) is subtle: the chain's quantisation had shown mostly on the darkest
  reflections, and the five classes cost ~80 ms per probe bake.
- 2026-09-13 M9 daylight: leaf-cluster shadow proxies (`SceneItem::shadowOnly`) so the sun
  dapples through the street trees onto the curtains and reveals at midday; sheer curtain
  panels (blended voile at 0.86, double-sided) that let the window show through faintly;
  `sun-window`/`sun-corner` audit views; `--volumetric` kept experimental (uniform medium).
- 2026-09-13 M9 lens look and exposure: `ExposureMeter` (compute shader, 64×36 cells,
  centre-weighted geometric mean with highlights over 12× the mean counted a twentieth; the
  draw-based first version rebound the DiscardContents scene target and lost the frame), the
  measured/analytic band widened to 2.5×, fixed-frame captures settle their exposure; the
  pipeline's film grain and chromatic aberration at subtle strengths and a `Vignette` quad
  multiplied over the finished frame (`--grain/--aberration/--vignette`, `--clean`).
- 2026-09-13 M9 planar reflections: `PlanarReflection` (mirrored view with the world reflected in
  the plane, oblique near plane on the glass for GL's −1 clip, half-float capture at half
  resolution, surface shader with Schlick Fresnel and the emissive picture on top), one plane for
  the television wall shared by the screen and the mirror's glass quad (`mirror_glass`), mirrored
  pass with flipped winding through the same PBR material path, `--no-reflections`,
  `--reflection-scale`, capture dump with `--dump-probes`; CNA_FINDINGS R-25. Then a capture per
  plane, the off-centre projection through the plane's corners (49 draws for the television
  wall from the entrance instead of 365, and the capture's pixels spent on the glass), and the
  window panes as a second plane from exposure 3 up: the pane draw becomes an additive Fresnel
  share of the mirrored room over the street (the television picture, sconces, shades and the
  reading lamp's bulb show in the glass at night, as they do in a real window). The rain
  droplets' normal map bends the pane lookup and its Fresnel normal, blended items are drawn
  into the captures, and the matrix side moved to `ReflectionMath` with a CPU unit test
  (`tests/ReflectionTests.cpp`). Screenshots: `docs/screenshots/m9-reflections-*.png`.
- 2026-09-13 M9 round 3: `FloatCubeUploader` (half-float irradiance cubes for probes and sky,
  `Irradiance.hpp` shared convolution), `--dump-probes DIR` and per-probe irradiance logging,
  shade radiance recalibration, `window-close` viewpoint, `CNA_FINDINGS` R-21 rewritten, R-23
  and R-24 added.

- 2026-09-13 M9 (audit round 2): exact CPU probe irradiance, glows dimmed in captures, ten more
  props and pictures, reading lamp, pouf loading; see §28.

- 2026-09-13 M9 (audit round 1): bloom retuned (3.0 / 0.12 / 4), low-sun sky chroma blended
  towards twilight blue away from the sun, probe bounce gain 1.25, sunset crash fixed,
  snow-dusted foliage and hedge, viewpoints `material` and `window` repointed. See §28.

- 2026-09-13 M9 (first round): lamps fade in over ~1 s and out in 0.4 s with a warm dimming
  tint (`RoomScene::update`, `Lamp::fullIntensity`), probes re-capture once the fade has
  settled; street lights and the lit windows opposite on their own photocell schedule (on below
  0.5°, off above 3°); `scripts/capture-views.sh` renders the 26-view audit set (day, evening,
  dusk, night, rain, storm, snow, overcast) with a contact sheet.

- 2026-09-13 M8: opaque items sorted front to back (early depth rejection before the PBR
  shader); shadow casters culled per cascade against the slice's box in a light-aligned basis
  (u, v extents across the light, nothing beyond the receivers along it) and skipped when
  smaller than six atlas texels of the slice: entrance 719 → 395 shadow draws (360 → 177 ms),
  tv-close 559 → 129 (305 → 29 ms), street 698 → 145 (357 → 14 ms); shadow distance 60 → 45 m; sky
  IBL products 16²/16 and 32²×5/12 samples (bake 262 → 105 ms), probe products 32²/16 (three
  probes in 2.3 s instead of 3.8); table-driven sRGB conversion in the mip builder; baked
  textures cached as PNG under `assets/cache/textures` (cold 26.8 s → warm 6.5 s at 1024², pixel-
  identical renders; `--texture-cache DIR|off`); `--ssao-samples`; per-pass GPU timing log.
  Post breakdown at 1280×720 on this VM: SSAO 201 ms, bloom 24, FXAA 18, fog 17, tonemap 9.

- 2026-09-13 M7: `AutoExposureEXT` measuring the HDR scene before `end()` (key 0.05, result
  clamped to ×/÷1.8 of the analytic schedule, `--exposure-mode auto|analytic`);
  `Render/TelevisionContent` (480×270 render target with a synthetic landscape / studio / test
  card programme, sRGB-encoded emissive on a picture quad in front of the model's screen, which
  has palette UVs); `Render/Overlay` (procedural 5×7 bitmap font atlas drawn with SpriteBatch:
  clock, weather, exposure, frame and GPU timings, camera; F1 / `--overlay`); bloom threshold
  set in display terms and divided by the exposure each frame (intensity 0.25, threshold 1.6);
  CNA height fog as aerial perspective by day from the weather's haze and rain; SSR tried on
  a rainy night and the glossy TV (R-17). Views: `docs/screenshots/m7-tv-night.png`,
  `m7-overlay.png`, `m7-fog-overcast.png`, `m7-ssr-rain-night.png`.

- 2026-09-13 M6: `Sim/WeatherSystem` (kinds, targets, transition table, temperature, wetness,
  snow cover, hail bursts, lightning envelope), `Render/Precipitation` (custom-shader particle
  volume for rain/snow/hail), `TextureBaker::raindrops` and `snow`, `RoomScene::applyWeather`
  (wet darkening/gloss, snow texture swap, pane droplets, curtain sway), lightning as
  `DirectionalLight2`, sky wind/stars/lightning from the weather; options and hotkeys.
  Views: `docs/screenshots/m6-rain-day.png`, `m6-snow-day.png`, `m6-storm-night.png`,
  `m6-rain-night-window.png`. A 150-frame run at 6× weather speed walked cloudy → overcast →
  rain → overcast → rain without errors.

- 2026-09-13 M5: `Sim/TimeOfDay` (solar clock, sun/moon paths, moon phase from age, HH:MM
  parsing); application schedules (lamp hysteresis, TV follows, analytic exposure adaptation,
  probe re-bakes on lamp change or ~3° of sun travel); incremental probe bake
  (`requestProbeBake`/`stepProbeBake`, one face per frame, products at the sixth) and the lamp
  cube shadow spread over three frames with per-face frustum culling; exposure now reaches the
  pipeline every frame (it was only applied at initialisation, so `--time` night renders were
  black); `--log-every N`. Two CNA findings written up in `CNA_FINDINGS.md` (R-2 cube readback
  after binding, which crashed the first sky rebake; R-1 tangents). Dusk run: 240 frames from
  17:55 at 1 real minute per day, lamps on at 18:11, exposure 10 → 55, no hitch above one frame
  of extra work. Views: `docs/screenshots/m5-entrance-0730/1200/1745/2200.png`.

- 2026-09-12 M4: `Exterior.cpp` — lawn/hedge/garden paths, two pavements with kerbs, 7.4 m road
  with centre dashes and bay lines, a terrace of 2-4 storey houses opposite (five facade
  materials, bays with framed sash windows, doors with steps, pitched tiled/slate roofs with
  gables and chimneys or flat parapets, a third of the windows lit at night), neighbours either
  side sharing our front plane, the storey above our room, a second row behind, a 26-block
  skyline; ten procedural trees (trunk, boughs, nine masked leaf blobs each), seven street
  lights (posts, arms, glowing luminaires, 36 000 lm punctual lights sharing the lamp switch);
  new bakers (foliage with alpha, bark, paving slabs, roof tiles) and 15 materials; coverage-
  preserving alpha mips; glass as reflection + transmission; per-cascade caster culling;
  viewpoints `street`, `street-left`. Views: `docs/screenshots/m4-street.png`,
  `m4-street-night.png`, `m4-street-night-right.png`.

- 2026-09-12 M3: interior environment probes (half-float capture with auto-detected readback
  orientation, two bounce iterations, per-item nearest probe; sky cast gone); `Lamp` model with
  lumen conversion (`Lamp::fromLumens`, 1 unit = 25 000 lux), one `PunctualLightEXT` per draw
  chosen by irradiance at the item, `CubeShadowMap` for the strongest lamp (pendant), lamp glows
  as emissive at radiometrically plausible levels; `--lamps/--tv` and hotkeys L/T; night sky
  calibrated (Rayleigh model faded below the horizon, twilight ends at -8°, moon/airglow ~30×
  real so a moonlit room shares an exposure with a lamp-lit one); TV screen as glossy black
  dielectric with the glTF emissive cleared; degenerate imported tangents repaired at load;
  extractor drops unsupported required extensions (pouf now loads); compiler warnings cleared.
  Views: `docs/screenshots/m3-entrance.png`, `m3-tv-close.png`, `m3-night-entrance.png`,
  `m3-night-sofa-to-tv.png` (`--sun -10,205 --exposure 60 --lamps on --tv on`).

- 2026-09-12 M2: asset manifest (22 GitHub-hosted files, SHA-256, licences), fetch script,
  `tools/gltf-extract` (Draco decode, WebP→PNG, node-regex subtree extraction, transform baking,
  recentring), 48 extraction recipes from the Bitterli White Room / Grey & White Room / Bedroom
  scenes plus 18 Khronos models; `ModelLibrary` (ContentManager import, per-part bounds from
  vertex data, material translation with per-slot UV transforms, mip rebuild, material tweaks);
  `RoomScene::buildFurniture`/`buildBookcase`: sofa, two armchairs, coffee table with magazine,
  fruit bowl, cup, candles, bottle; rug; floor lamp; pedestal table with lamp and bottles; pouf;
  fireplace surround with stove and wall-mounted TV; plants; sconces; chest of drawers with vase
  and radio; small pictures; damask chair; curtains and rods; large picture; slipper chair;
  walnut bookcase with books, boxes, toy car, boombox, teapot, wicker ball; paper globe pendant.

- 2026-09-12 M1: `Camera`/`CameraController` (WASD + arrows + Q/E, Shift/Ctrl, smoothed,
  frame-rate independent); `MeshBuilder` (CNA winding, world-grid UVs, tangents); `GpuMesh`;
  procedural `TextureBaker` (plaster, oak planks with per-plank variation and metric gaps,
  carpet, weave, painted wood, concrete, brick, asphalt, grass, flat) uploaded with linear-light
  mip chains; `MaterialLibrary` (25 materials); `SkySystem` (CNA atmospheric model + clouds,
  stars, moon, twilight, haze; CPU sun/ambient; IBL products via `EnvironmentProcessor`, scale
  auto-calibrated so a 60° sun's zenith is 0.30 of sunlit white); `SceneRenderer` on
  `RenderPipeline` (RGBA16F, ACES, bloom, FXAA, SSAO from `DepthNormalPrepass`, sorted
  transparency, GPU timers) with a 3-cascade `CascadedShadowMap`; `RoomScene` architecture
  (6.2×4.6×2.7 m, 0.30 m exterior walls, two windows with casements/glazing bars/sills/reveals,
  door with architrave, panels and lever handle, baseboards, cornice, radiator, outlets, switch,
  pendant, placeholder exterior); viewpoints, `--view/--camera/--sun/--clouds` etc.

- 2026-09-12: reconnaissance of CNA `next` @ `1b3151f2f`, sharp-runtime `next` @ `0c82d9b8`,
  easy-gl `develop` @ `deda7a4`, meta-gl `develop` @ `20c8b2d`; environment probing; project
  bootstrap builds and renders headlessly (`screenshots/smoke.png`).

## 30. Measured results

| Date | Build | View | Resolution | Frame (ms) | Notes |
|---|---|---|---|---|---|
| 2026-09-12 | bootstrap | clear only | 1280×720 | ~n/a | 3 frames + PNG readback in 3.5 s wall |
| 2026-09-12 | M1 | entrance | 1280×720 | 251 CPU / GPU shadow 17 + prepass 4 + opaque 102 + post 128 | llvmpipe; 25 draws + 57 shadow draws, 2.2k tris; SSAO 16 samples full-res dominates post |
| 2026-09-12 | M2 | entrance | 1280×720 | 647 CPU (shadow 209, prepass 66, opaque 62, post 310) | llvmpipe; 178 draws + 570 shadow draws (3 cascades × 190 casters, no culling yet), 605k tris |
| 2026-09-12 | M3 | entrance | 1280×720 | 697 CPU (shadow 235, prepass 74, opaque 72, post 315) | llvmpipe; 187 draws + 597 shadow draws, 644k tris; content load 22 s of which probe bake 9.7 s, materials 1.5 s (512² textures) |
| 2026-09-13 | M9 load | content load, 320×180, 128² procedural textures | — | glTF import 22.0 s; .cnb 21.9 s (55 of 64 models; the load step halves, the prepare step is the cost); + shared rebuilt textures (117 copies) and imported textures capped at 1024 px: 17.5 s (readback 3.4 s, mip build 5.6 s); + threaded `Image::halved` and the row/column signature: 14.9 s (readback 3.7 s, mip build 2.6 s of which the GL uploads are most, signatures 0.16 s, 115 copies shared) | this VM; `--model-texture-size 0` keeps full size |
| 2026-09-13 | M9 round 6 | entrance, noon cloudy | 960×540 (512² textures) | 1784 CPU (shadow 332, prepass 138, reflection 18 [51 draws], opaque 681, post 615) | this VM, while a build ran alongside; 292 draws + 414 shadow; lens look on |
| 2026-09-13 | M9 round 6 | entrance, 22:00 | 960×540 | 1272 CPU (shadow 305, prepass 122, reflection 88 [164 draws], opaque 382, post 373) | two reflection planes (television wall, panes); 292 draws + 558 shadow |
| 2026-09-13 | M9 round 6 | tv-close, noon | 960×540 | 948 CPU (reflection 236 [216 draws], opaque 220, post 464) | the mirrored view of a close screen covers the whole room: the capture's draw count is its cost |
| 2026-09-13 | M9 round 6 | sun-window, 13:00 clear | 960×540 | 960 CPU (shadow 141, opaque 67, post 734) | post dominates: SSAO + bloom + FXAA + grain/aberration at 960×540 on llvmpipe |
| 2026-09-14 | M9 sunbeams | window, sun 45/180 clear | 1280×720 (1024² textures) | 1294 CPU (shadow 128, prepass 50, opaque 271, beams 214, post 844) | the beam march at full size, 24 steps with one atlas tap each; the sun now enters the room (the front skin cut) |
| 2026-09-14 | M9 round 18 | entrance, noon cloudy | 1280×720 (1024² textures) | 1644 CPU (shadow 245, prepass 121, reflection 19 [60 draws], opaque 398, post 862) | 328 draws + 462 shadow, 843k tris (the facades' recesses, the props, the canvases, the clock); the beams' 210 ms land in "post" (asynchronous GL) |
| 2026-09-14 | M9 half beams | window, sun 45/180 clear | 960×540 (512² textures) | 850 CPU beams off / 856 half-size march / 939 full-size march (the least of frames 5-16) | the half-size march with the bilinear upsample costs within the frame's noise; the full-size march about 90 ms here |
| 2026-09-14 | M9 round 23 | entrance, noon cloudy | 1280×720 (1024² textures) | 1532 CPU | 339 draws + 488 shadow: the drum wall lights, the window lights, the throw, the book, the post and keys; the gallery's hero frame |
| 2026-09-14 | M9 round 23 | sofa-to-tv, 22:00 clear | 1280×720 | 1595 CPU | the fire, the candle, the steam, the picture at its dark-room level, the sconces glowing as drums |
| 2026-09-14 | M9 round 23 | corner, 10:30 clear | 1280×720 | 1770 CPU | the sun's patches on the floor, the half-size beam march, the motes |
| 2026-09-14 | M9 round 23 | the pavement camera, 22:00 rain | 1280×720 | 877 CPU | outside the room: the street, its puddles and the chimney smoke off (a mild night) |
| 2026-09-14 | M9 round 18 | sofa-to-tv, 22:00 clear | 1280×720 | 1640 CPU (shadow 395, prepass 57, reflection 142 [180 draws], opaque 73, post 974) | the stove's fire and the candle burning; two reflection planes (television, mirror) |
| 2026-09-13 | M9 DoF | material, noon cloudy | 960×540 | 1011 CPU (shadow 304, prepass 48, reflection 48, opaque 300, post 311 with DoF; 235 without) | the depth-of-field pass costs ~90 ms of post here; the autofocus readback is under a millisecond |
| 2026-09-14 | M9 wet street | street, 22:00 rain | 960×540 | 659 CPU (reflection 197 [304 draws: the panes' capture of the room plus the street's of the houses], opaque ~120, post ~250) | the street capture is 105 exterior draws, 20-40 ms; dry it costs nothing (the plane is disabled) |
| 2026-09-14 | M9 wet street | the pavement camera `0,1.3,-8,0,-10`, 22:00 rain | 960×540 | 407 CPU (reflection 5 [63 draws], opaque 67, post 319) | outside the room nothing but the street draws |
| 2026-09-13 | M8 | entrance, noon | 1280×720 | shadow 177 (was 360), prepass ~110, opaque ~150, post ~800 CPU | 269 draws + 395 shadow; warm start 21.9 s (materials 6.5 s cached at 1024²) |
| 2026-09-13 | M8 | tv-close / street, noon | 1280×720 | shadow 29 / 14 ms (were 305 / 357) | 129 / 145 shadow draws after the light-space cull |
| 2026-09-13 | M7 | entrance, noon, overlay | 1280×720 | 1366 CPU (shadow 344, prepass 110, opaque 146, post 765) | this VM (see §26 note); 269 draws + 719 shadow, 672k tris; GPU opaque 616 post 281 |
| 2026-09-13 | M7 | sofa-to-tv, 21:00, TV on | 1280×720 | 1108 CPU | measured luminance 0.0006, exposure 63 |
| 2026-09-13 | M5 | window, dusk run 17:55→19:31 | 640×360 | 590-740 CPU; lamp switch frame 293 (cube shadow 2 faces), probe steps +~80 | 256² textures; previous synchronous rebuild was one 8 s frame |
| 2026-09-12 | M4 | street (right window) | 1280×720 | 653 CPU (shadow 216, prepass 5, opaque 38, post 394) | 117 draws + 700 shadow draws of 387 items, 43k tris; content load 14.3 s (materials 2.2 s at 512²) |
| 2026-09-12 | M4 | street, night, lamps | 1280×720 | 570-609 CPU | post dominates (SSAO + bloom + FXAA at 720p on llvmpipe) |
| 2026-09-12 | M3 | entrance, night, lamps | 1280×720 | 665 CPU (shadow 209, prepass 70, opaque 67, post 319) | + one cube shadow for the pendant; probe peak radiance 1.24 (pendant shade) |

## 31. Major CNA functionality exercised (keep expanding)

- `ComputeShader` + `StorageBufferT<float>` (the exposure meter's cell reduction, ES 3.1
  compute); `RenderPipelineSettings` film grain and chromatic aberration; a custom `BlendState`
  (destination × source) for the vignette quad after `RenderPipeline::end()`.
- `RenderTargetCube` (`HalfVector4`) filled face by face through `SetRenderTarget(cube, face)`
  and a `ShaderEffect` quad; `RenderTarget2D` (`HalfVector4`, `Depth24`) as a planar reflection
  capture sampled by a `ShaderEffect` through `SetTexture`; `Matrix::CreateReflection`,
  `Plane`, `BoundingFrustum` from an oblique projection.

- [x] `Game` / `GraphicsDeviceManager` / `GameWindow`
- [x] `GraphicsDevice::Clear`, `GetBackBufferData`, `Texture2D::CreateFromPixels/SaveAsPng`
- [x] `PbrEffect` (+ IBL, cascades, one punctual light per draw, `setShadowFilterRadiusEXT` from the cloud deck)
- [x] `RenderPipeline` HDR/bloom/tonemap/FXAA
- [x] `CascadedShadowMap` (fitted to the camera, per-cascade caster culling; its atlas and cascade matrices also feed the room's own volumetric sunbeam march)
- [x] `DepthNormalPrepass` + `SsaoPass` (SSR/contact shadows wired, off by default)
- [x] `DepthOfFieldPass` (autofocus from the prepass depth read back on the CPU)
- [x] `LensFlarePass` (wired, off by default: the pendant's disc throws ghosts too large)
- [x] `AtmosphericSky` model GLSL + `EnvironmentProcessor` IBL (own sky shader; `Skybox` not used)
- [ ] `LightProbeVolumeEXT` / `LightProbeBaker`
- [x] `CubeShadowMap` (the pendant, two faces a frame); `SpotShadowMap` not used
- [ ] `ParticleSystem`
- [ ] `InstancedRendererEXT` / `FrustumCullerEXT` / `LodGroupEXT`
- [x] sorted transparency through `RenderPipeline::setTransparentScene` (own sort)
- [x] `ShaderEffect` custom shaders
- [x] `ContentManager::Load<Model>` glTF import (Model/ModelMesh/ModelMeshPart/ModelBone, PbrEffect material read-back, `VertexBuffer::GetDataRawEXT`)
- [x] `GpuTimer` per stage + pipeline pass timings (`DebugDraw` pending)
- [ ] `AutoExposureEXT` (compute)
- [ ] `AreaLightEXT`, `DecalPass`, `ColorGradePass`, `LensFlarePass`
