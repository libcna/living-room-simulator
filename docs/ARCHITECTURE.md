# Architecture map

The executable is a CNA `Game` subclass. The main loop in `RoomApplication` owns the camera, clock, weather, assets, scene and renderer. `RoomScene` creates room geometry, imported furniture and viewpoints, then registers scene items with `SceneRenderer`. `SceneRenderer` renders shadows, probes, reflections, the opaque and transparent scene and postprocessing. `CnaRoom::Effects` contains effects adapted from CNA's retired engine; their Ms-PL notices remain with those files.

| Area | Files | Responsibilities |
|---|---|---|
| App lifecycle and CLI | `src/Main.cpp`, `src/RoomApplication.cpp` | CNA window and device, arguments, input, scheduling and screenshots |
| Scene | `src/Scene/RoomScene.cpp`, `src/Scene/RoomSceneFurnishings.cpp`, `src/Scene/Exterior.cpp` | Procedural geometry, furniture placement, dynamic props and exterior |
| Materials and models | `src/Render/MaterialLibrary.cpp`, `src/Assets/ModelLibrary.cpp`, `src/Assets/TextureBaker.cpp` | PBR surfaces, glTF import and procedural textures |
| Frame rendering | `src/Render/SceneRenderer.cpp`, `src/Render/SceneRendererProbes.cpp`, `src/Render/SkySystem.cpp`, `src/Render/PlanarReflection.cpp` | Light selection, sky, probes, reflections and draw order |
| Exposure | `src/Render/ExposureMeter.cpp`, `src/Render/ExposureStatistics.cpp`, `src/Effects/AutoExposureEXT.cpp` | Scene luminance measurement and exposure adaptation |
| Effects | `src/Effects/` and `include/CnaRoom/Effects/` | Shadows, prepass, postprocessing and shader packages |
| Simulation | `src/Sim/` | Solar clock and weather transitions |

The exposure is image-based and uses only public CNA graphics calls. After `RenderPipeline::end()` the HDR scene target is unbound but still holds the frame; `ExposureMeter` draws it through a fullscreen vertex/fragment pass (`src/Render/shaders/luminance_meter/`) into a 64 x 36 `SurfaceFormat::Color` target, each texel the Rec.709 luminance of an 8 x 8 box of taps over its cell, packed as a 24-bit `log2` code across R, G and B so the readback is a plain `GetData` on every renderer. `ExposureStatistics` decodes the cells and computes the centre-weighted, highlight-rejecting geometric mean the measured exposure follows and the plain geometric mean `AutoExposureEXT` adapts to; both come from the one reading per frame. Where the renderer does not execute shader-effect source the meter reports why and image-based exposure is unavailable. The simulator keeps no GPU timing; the frame statistics are CPU stopwatch times.

`RoomScene` owns procedural `GpuMesh` objects; renderer items borrow their pointers. `RoomApplication` owns the scene and renderer and controls their lifetime. Imported models are retained by `ModelLibrary`. Changes to ownership or cleanup order need a render run, not only CPU tests.

The largest file is still `SceneRenderer.cpp`. Probe capture and integration now live in `SceneRendererProbes.cpp`; both files implement the same class and share its ownership model. Furniture and lamp construction similarly live in `RoomSceneFurnishings.cpp`, separate from room architecture and simulation. Keep new responsibilities in the relevant subsystem rather than expanding these files further. A gradual split is appropriate when changing renderer setup, reflection capture or draw passes. Preserve draw order and resource lifetime while doing so, using the visual scenarios to verify each extraction.

`CMakeLists.txt` builds the application code as `cna_room_core` so tests link the same code. `tests/` has CPU tests and one headless render smoke test. The rendering pipeline is specific to the EasyGL baseline; review `docs/KNOWN_ISSUES.md` and `CNA_FINDINGS.md` before changing an old CNA workaround.
