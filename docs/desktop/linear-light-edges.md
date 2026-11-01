# Linear-light edges in NXCanvas (R2)

NXCanvas now blends the anti-aliased edges of shapes in linear light. Every desktop and NxUI shape with a soft edge looks cleaner: buttons, rounded windows, circles, strokes, icons, and rounded clips. Fully covered pixels, text, and shadows are unchanged. There is no API change.

## Why

A pixel on a shape's edge is partly covered. 40% coverage means 40% of the pixel's light should come from the shape. Before R2, NXCanvas mixed the two colors in their stored sRGB values. sRGB values are gamma-encoded, so they are not proportional to light, and a 50/50 mix in sRGB is darker than half the light. The result was that:

- light shapes on dark backgrounds (most of the UI in dark mode) had edges that looked thin and jagged;
- dark shapes on light backgrounds had edges that looked heavy.

macOS, Windows (Direct2D) and Skia-based browsers all blend coverage this way or correct for it.

## What changed

- **Edges** (`desktop/lib/nxcanvas/nxc_core.c`). When a pixel is partly covered and the pixel under it is opaque, NXCanvas does two things:
  1. It works out the fully covered result exactly as before.
  2. It mixes that result with the old pixel by the coverage, in linear light: `out = toSRGB(cov · lin(full) + (1 − cov) · lin(dst))`.
- **Alpha.** Paint alpha and the canvas's global alpha still composite in sRGB, so translucent fills and glass look the same as before. Only geometric coverage is linear. That covers path and shape coverage, partial rect edges, rounded-clip edges, and gradient shape edges.
- **Translucent targets** have no single display color, so they keep the old blend.
- **Unchanged blends.** These callers use `nxc__span_cov_gamma` and `nxc__span_solid_gamma`, which are bit-for-bit the old blend:
  - **Text** (`nxc_text.c`) already has its own per-polarity coverage correction, tuned for the sRGB blend. A text-rendering A/B with cached sizes, a 250 px uncached size and translucent text produced the same image hash before and after.
  - **Blurred shadows** (`nxc_effects.c`). Their falloff is a designed curve, not geometry.
  - **Caller masks** (`nxc_draw_mask`).
- **Lookup tables** (`desktop/lib/nxcanvas/nxc_srgb_lut.h`). The engine has no math library, so the tables are generated on the host by `desktop/lib/nxcanvas/tools/gen_srgb_lut.c` from the sRGB transfer function. The generator refuses to write tables where an sRGB value does not survive the round trip. That guarantees a color blended with itself never changes.
  - 8-bit sRGB to 16-bit linear: 256 entries.
  - 12-bit linear to sRGB: 4096 entries.
- **Cost.** Only partly covered pixels pay: three table reads and one weighted average per color channel. Filled interiors are untouched. In QEMU (2 GiB, 1 CPU), the app drawer was toggled 8 times. The busiest 5 s window had about 240 full redraws, and desktop draw time per drawn frame was 25 and 33 ms before and 27 and 33 ms after, over two runs each. That is within run-to-run noise.

## Tests

- `desktop/lib/nxcanvas/tests` (`make all-tests`): every suite passes. The registry suite already fails to build on `main` because of an include path.
  - **New in `test_core`:**
    - white over black along a 0..255 coverage ramp gives light equal to coverage (worst error 0.004) and is monotonic;
    - the `_gamma` spans keep the old mix;
    - a color blended with itself is unchanged for all 256 values and all coverages;
    - global alpha still composites in sRGB;
    - translucent targets keep the old blend.
  - **`test_raster`:** its area checks now measure light, which is correct for linear coverage. Its alpha checks read the sRGB value.
- `tests/host/nxgfx_canvas_test.c`: 31 checks pass.
- QEMU: fbtest, nxwtest and nxuitest work, and wintest gives 39 PASS. Before and after crops are in the R2 pull request.
