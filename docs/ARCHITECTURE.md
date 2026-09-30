# Architecture map

The executable is a CNA `Game` subclass. The main loop in `RoomApplication` owns the camera, clock, weather, assets, scene and renderer. `RoomScene` creates room geometry, imported furniture and viewpoints, then registers scene items with `SceneRenderer`. `SceneRenderer` renders shadows, probes, reflections, the opaque and transparent scene and postprocessing. `CnaRoom::Effects` contains effects adapted from CNA's retired engine; their Ms-PL notices remain with those files.

| Area | Files | Responsibilities |
|---|---|---|
| App lifecycle and CLI | `src/Main.cpp`, `src/RoomApplication.cpp` | CNA window and device, arguments, input, scheduling and screenshots |
| Scene | `src/Scene/RoomScene.cpp`, `src/Scene/RoomSceneFurnishings.cpp`, `src/Scene/Exterior.cpp` | Procedural geometry, furniture placement, dynamic props and exterior |
| Materials and models | `src/Render/MaterialLibrary.cpp`, `src/Assets/ModelLibrary.cpp`, `src/Assets/TextureBaker.cpp` | PBR surfaces, glTF import and procedural textures |
| Frame rendering | `src/Render/SceneRenderer.cpp`, `src/Render/SceneRendererProbes.cpp`, `src/Render/SkySystem.cpp`, `src/Render/PlanarReflection.cpp` | Light selection, sky, probes, reflections and draw order |
| Effects | `src/Effects/` and `include/CnaRoom/Effects/` | Shadows, prepass, postprocessing and shader packages |
| Simulation | `src/Sim/` | Solar clock and weather transitions |

`RoomScene` owns procedural `GpuMesh` objects; renderer items borrow their pointers. `RoomApplication` owns the scene and renderer and controls their lifetime. Imported models are retained by `ModelLibrary`. Changes to ownership or cleanup order need a render run, not only CPU tests.

The largest file is still `SceneRenderer.cpp`. Probe capture and integration now live in `SceneRendererProbes.cpp`; both files implement the same class and share its ownership model. Furniture and lamp construction similarly live in `RoomSceneFurnishings.cpp`, separate from room architecture and simulation. Keep new responsibilities in the relevant subsystem rather than expanding these files further. A gradual split is appropriate when changing renderer setup, reflection capture or draw passes. Preserve draw order and resource lifetime while doing so, using the visual scenarios to verify each extraction.

`CMakeLists.txt` builds the application code as `cna_room_core` so tests link the same code. `tests/` has CPU tests and one headless render smoke test. The rendering pipeline is specific to the EasyGL baseline; review `docs/KNOWN_ISSUES.md` and `CNA_FINDINGS.md` before changing an old CNA workaround.
