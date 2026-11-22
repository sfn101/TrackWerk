# More visualizers

Asked for 2026-11-20 (no design Q&A: the user gave the design directly).

## Goal

Fourteen more visualizer styles to look at, in the same place and the same way as the nine there are:
the scope strip at the top, the VI screen, SETTINGS > VISUALIZER and VI's A+LEFT/RIGHT.

## The styles

Appended to `VisualizerType` after PLASMA (settings and themes store the number, and themes the name):

| Name (row) | Saved as | What it is |
|---|---|---|
| TUNNL | TUNNEL | A textured tunnel rushing toward you |
| FIRE | FIRE | Demoscene fire rising from the bottom; louder music, taller flames |
| RAIN | RAIN | Columns of falling glyphs |
| TWIST | TWISTER | A twisting four-sided bar |
| RINGS | RINGS | A shockwave on every hit, faint rings between, the core shaped by the waveform |
| KALEI | KALEIDO | A kaleidoscope: a flowing pattern in six mirrored slices, zooming in on each hit |
| BLOBS | BLOBS | Metaballs drifting and merging |
| MAZE | MAZE | A maze seen through a slowly turning, breathing lens (replaced the checker rotozoom) |
| CUBE | CUBE | Two nested wireframe cubes (light 3D) |
| GLOBE | GLOBE | A wireframe globe with spikes that shoot out with the music |
| LAND | TERRAIN | A wireframe landscape flying by; the music raises the hills |
| TORUS | TORUS | A wireframe torus |
| BALLS | DOTBALL | Vector balls on a rotating sphere, sized and shaded by depth |
| RIDGE | RIDGES | Stacked waveform lines receding into the distance |

## Rules

- **Theme colours only:** several shades per style, all from the theme (the demo ramp ground → panel →
  accent → ink, plus the wave, title and selection colours). They follow every theme and scene.
- **Light:** software canvas, cells of 4-6 px for per-pixel effects, a few hundred lines for 3D, no
  allocation while drawing, fixed-size state for FIRE and RIDGES. Each frame well under the cost of
  PLASMA at the same size.
- **Smooth, dramatic, and with the song (changed 2026-11-21 after two hand tests):** motion never jumps. Each visualizer listens through one shared step (`demo_fx::listen`): the loudness smoothed and measured against the song's own recent peak (so a quiet song drives as hard as a loud one, and a loud one cannot overreact); a kick that punches on each hit and fades in about a quarter second; and a music clock that only moves forward, 0.5× in silence, faster as the song drives, with a rush on each hit (at most 4×). Every style uses all of it, upstream's COPPER, STARFIELD and PLASMA too (their look unchanged), and a hit adds a faint flash. LISSAJOUS, too faint to read as dramatic, was replaced by KALEIDO; RINGS fires a shockwave per hit; GLOBE grows spikes. Upstream's own styles are never removed. Third hand test: everything a little calmer (at most 2.9× on a hit), BLOBS small soft drops with a thin skin, TWIST and RIDGE toned down, FIRE without stray hot spots, COPPER's swing slower, ROTOZOOM replaced by MAZE (the one-line diagonal maze through a slowly turning lens), and SCOPE / OCTA soft-limited so a loud song rounds off short of the edge instead of clipping.
- **Still when stopped:** like COPPER, STARFIELD and PLASMA, they hold their last frame while no song
  plays (or with Motion off).
- Any size: the 620×70 strip, the 834-wide strip in 16:9, and VI.
- Written for TrackWerk (classic demoscene effects); no third-party code, so no new licence notice.

## Platforms

Shared UI code only; every platform gets them.

## Tests

- Every style draws at strip and VI size without leaving its box: `ptvisualizer-check` at 4:3, wide mode's own `ptwide-mode-check` at 16:9 (it reads the style list from the app, so neither feature needs the other).
- Every style saves and loads by name in a theme, and by number in settings.json.
- VI's A+LEFT/RIGHT walks all 23 and wraps.
- Each style's frame time stays light (timed in the check, against PLASMA).

## Out of scope

- New audio analysis (they use the waveform and its loudness, as the others do).
- Per-style settings.

## Help

- SETTINGS > VISUALIZER's and VI's help: more styles to step through.
- Manual: the new styles in the visualizer list.
