# Current issues and maintenance queue

This is the current entry point for maintenance work. `CNA_FINDINGS.md` contains the full historical evidence and the CNA status review of 2026-09-30. The archived `plan.md` and `NEXT.md` are not an active backlog.

1. **Find the remaining source of non-finite HDR pixels.** The locked CNA baseline still produces 2318–2351 invalid raw pixels in the 320×180 smoke scene, despite the upstream PBR fix. `FiniteHdrPass` reduces the count to zero before bloom and must remain enabled. Removing the old tangent repair did not change that count or the rendered pixels. Use `CNA_ROOM_TRACE_POST=1` with the render smoke test to reproduce it.
2. **Review half-float cube upload.** CNA now supports direct half-float cube transfers, but the simulator still uses `FloatCubeUploader` and its fallback. Replace it only after checking probe orientation, night irradiance and specular reflections against the locked baseline.
3. **Renderer scope.** The maintained native baseline is `OPENGLES3`. WebGL2 needs a fresh build and browser render against the current lock. Other renderer families and operating systems need compatibility work and separate reference images. Raw-GLSL content added to a planar reflection capture would need its GL clip convention handled separately from stock PBR effects.
4. **Visual coverage.** The automated render test checks a small scene for a non-black image. Full visual review uses the deterministic captures in `docs/VISUAL_TESTING.md`. CI currently runs the CPU suite; adding a reliable graphics runner is future work.
5. **Scene and performance.** Contact shadows remain disabled because the pass rims silhouettes. Motion blur is wired but not evaluated in a clip. The old measured frame times were from Mesa llvmpipe, not a hardware GPU target.

When taking an item, record the CNA revision, commands, before/after images and result in the change or issue. This file should contain only unresolved work; move resolved details into the historical findings or Git history.
