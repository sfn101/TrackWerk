# Sharp scaling

Brainstormed 2026-11-18, agreed 2026-11-20 after a grill-me session. Built before Wide mode (roadmap 10).

## Goal

Today SETTINGS offers two ways to put the picture on a screen whose size isn't a whole multiple of the design size. Both
have a drawback:
- **INT** (`INTEGER`) stays crisp but only enlarges by whole numbers, so a 960×544 RG505 or a 1280×720 Smart Pro shows a
  small 1× picture.
- **BILINEAR** (`FIT`) fills the screen but blurs.

SHARP fills the screen and stays nearly as crisp as INT.

## The setting

- `SCALING: INT / SHARP / BILINEAR`, crispest to softest.
- The saved `scalingBilinear` flag becomes a three-way value. Existing settings carry over: `true` becomes BILINEAR,
  `false` becomes INT.
- **Default for new installs is SHARP, on every platform.** A saved choice is kept.

## How SHARP works

1. **Enlarge.** The finished canvas (640×480, or 854×480 in wide mode) is enlarged by the smallest whole number N that
   makes it at least as big as the target, using nearest-neighbour into a render-target texture. N is capped so that
   texture stays within the GPU's maximum texture size.
2. **Shrink.** That texture is drawn to the largest aspect-correct rectangle that fits the output, with linear filtering.

- **Same size as BILINEAR.** The frame rectangle equals today's FIT rectangle, so the letterbox bars and the Android touch
  buttons get the same room as with BILINEAR.
- **Where it lives:** `shell/sdl/sdl-video.cpp` (`set_scaling`, `dest_rect`, the present path). One change covers Windows,
  Linux, the handhelds and Android (minSdk 26).
- Scaling happens after the frame is drawn, so it never changes the canvas pixels.

## Fallback

If the renderer can't create a render-target texture (some old Android GPUs, SDL's software renderer):
- the app uses BILINEAR;
- SETTINGS shows `SHARP (USING BILINEAR)`;
- the log records the reason once.

## Kept as today

- Bars keep the theme's letterbox colour.
- Every theme, font and surface style looks the same, since the finished picture is scaled.
- Android's system bars are unchanged: hidden, edge to edge, and shown over the app by a swipe. No space is reserved for
  them unless a real device shows a problem.

## Tests

- A host check of the size maths: N, the intermediate size, and the final rectangle for 960×544, 1280×720, 1920×1080,
  2400×1080 and 640×480, at 4:3 and at 854×480.
- Every existing canvas pixel hash is unchanged.
- A settings check: old `true`/`false` values load as BILINEAR/INT, and a new install gets SHARP.

## Out of scope

- Shader-based scalers (CRT and similar). The existing screen overlay covers that look.
- Per-screen scaling.

## Help

- SELECT help for the SETTINGS `SCALING` row (`help_text.h`): the three modes and the fallback text.
- Manual: the SETTINGS section describes INT, SHARP and BILINEAR.
