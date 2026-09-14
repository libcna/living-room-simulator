# NEXT — continuation queue

Read `plan.md` §CONTINUATION RULE first. This file is the short queue for the next iteration.

1. M9 audits, next round (round 13 renders into `screenshots/audit13/` with the road out from
   under the grass, the puddles and the 16 m prepass plane): log findings in `plan.md` §28;
   standing candidates: dusk facade saturation, the pendant's disc in the views beneath it.
   Done in round 22: the sconces rebuilt as drum wall lights; the windows as spot lamps against
   the interior's flatness under overcast light; then nine probes in three rows with the
   floor in sections (a per-item probe still paints each section one level; the ceiling and
   walls stay whole because a step on plain plaster reads as an edge; probes blended per
   pixel would be the real cure).
1a. Done: prefiltered specular in half-float as one cube per roughness class (the material
   picks the class from its roughness and its map's mean). Per-pixel roughness variation
   within one material still blurs at one class; a CNA EXT binding a cube face at a mip level
   would allow a real chain.
1b. Planar reflections, next: the reflection's own shadows (the cascades are fitted to the main
   camera, so a capture looking elsewhere is unshadowed there), a cheaper capture (it redraws
   the room with full materials; a lower LOD or fewer lights would do). Done: the panes bent by
   the droplets, the wet street, its puddles (and the road under the grass that had hidden it).
1c. Direct sun by day: done in M9 round 16, twice over. The house's front skin (one box
   across the facade, windows included) had cast a shadow over every window since M4, so no
   direct sun had ever reached the room; it is now pieces around the openings, and the street
   trees' shadow proxies dapple what comes in. Sunbeams: an own ray-marched pass from the
   cascade atlas and the prepass depth (`--sunbeams`), since the pipeline's volumetric fog
   cannot be given a shadow map (R-26). Next: a half-size march (214 ms at 720p), the lamps'
   own volumetrics at night (the cube shadow map for the pendant), dust motes in the beams.
1d. Done in M9 rounds 17-18: dust motes in the beams, a wall clock that keeps the scene's
   time, the wood stove lit on cool evenings (embers, flame cards, a flickering lamp), a candle
   lit with the lamps, the street facades with recessed windows, sills, plinths, gutters and
   downpipes, a bench, a bin and a bicycle across the road; in rounds 19-20 three parked cars
   (lofted procedural bodies), a shop front on the terrace opposite, and the beam march at
   half size (its cost now within the frame's noise), the television's light on the room
   following its picture, steam over the cup, the sconces rebuilt as linen drum wall lights.
   Handles on the windows, a throw over the sofa's back, an open paperback, post and keys on
   the chest are in, chimney smoke across the street on cold days, and the door ajar on a lit
   hall with a coat and shoes, the shop's sign box and a bus stop lit at night, window boxes,
   dishes and aerials on the facades, and the trees' crowns sway with the wind (their shadow
   proxies with them). Next of that kind: slippers by the sofa, a newspaper on the armchair, a
   passer-by on the pavement, a bag by the hall door.
2. Exposure: the meter is in; a histogram measure could follow if the pendant views still drift.
3. Done earlier: street lights switch on their own elevation thresholds (0.5 / 3 degrees) and
   the lamps and street lights ramp over a second (`RoomScene::update`).
4. Done: `.cnb` per model (`scripts/compile-assets.sh`); it halves the load step but the
   content time was in the prepare step (mip rebuild readbacks, tangent repair), now cut by
   sharing rebuilt textures by content signature (rows and columns, after a collision took a
   lamp's bulb), capping imported textures at 1024 px and a threaded `Image::halved`: 22 -> 15 s.
   Left: the readbacks (3.7 s; the sidecar PNG could be decoded instead of read back from the
   GPU copy) and the per-level GL uploads.
5. Instancing for books and repeated exterior chunks: `InstancedRendererEXT` takes a
   `ModelMeshPart` and the stock `PbrEffect` reads the per-instance matrix from attribute
   locations 12..15, so imported models (the book rows, the chairs) qualify; procedural
   `GpuMesh` items would need a part built around their buffers. Little to gain on llvmpipe,
   where the draw count is not the cost.
6. Contact shadows (`ContactShadowPass` exists but the pipeline has no hook).
7. Done: depth of field with centre-point autofocus (`--dof`, `--focus`). Next for the lens: the
   pipeline's lens flare and motion blur passes (a walk through the room at 24 fps), colour
   grading with a LUT for the night look.
