# Visual testing

The checked-in gallery is a historical visual reference. It was captured before the current CNA migration, so differences are reviewed by a person; the files are not byte-for-byte golden images. Keep camera, time, weather, resolution and assets consistent when comparing. The four quick scenarios cover daylight, night lighting, direct sun and rainy reflections.

```sh
scripts/capture-views.sh --quick --smoke --bin build-dev/bin/living-room-simulator \
  --out screenshots/review
```

Inspect `screenshots/review/contact-sheet.png` and the individual images. Compare composition, missing objects, severe clipping, black or non-finite regions, lighting, shadows, window glass and reflections with the curated gallery in `docs/screenshots/`. For a rendering change, run the complete 28-view set too:

```sh
scripts/capture-views.sh --quick --bin build-dev/bin/living-room-simulator \
  --out screenshots/audit
```

The script must return nonzero if any capture fails. The quick set uses 960×540 and smaller textures; it is a review set, not a performance benchmark. `ctest --preset dev` includes a 320×180 headless test that asserts a real, non-black PNG. On headless Linux these commands need working Xvfb, X11 and Mesa. A missing display is an environment failure, not evidence of a rendering regression.

To revisit one capture after a fix, pass `--case NAME`, for example `--case rain-day-street`. The command returns an error for an unknown name. The generated contact sheet contains only the views from that invocation.

For a change in the draw pipeline, also inspect the mirror, rainy pane and wet-street views in the full set. Keep a before/after contact sheet with the change. Pixel tolerances and cross-machine golden comparisons are intentionally not specified until a runner and renderer revision are stable.
