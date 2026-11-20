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
| RINGS | RINGS | Rings flowing out of the centre, the inner one shaped by the waveform |
| LISSA | LISSAJOUS | A Lissajous figure bent by the waveform |
| BLOBS | BLOBS | Metaballs drifting and merging |
| ROTO | ROTOZOOM | A rotating, zooming checker plane |
| CUBE | CUBE | Two nested wireframe cubes (light 3D) |
| GLOBE | GLOBE | A wireframe globe with an orbit, like the splash's |
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
- **Still when stopped:** like COPPER, STARFIELD and PLASMA, they hold their last frame while no song
  plays (or with Motion off).
- Any size: the 620×70 strip, the 834-wide strip in 16:9, and VI.
- Written for TrackWerk (classic demoscene effects); no third-party code, so no new licence notice.

## Platforms

Shared UI code only; every platform gets them.

## Tests

- Every style draws at strip and VI size, 4:3 and 16:9, without leaving its box.
- Every style saves and loads by name in a theme, and by number in settings.json.
- VI's A+LEFT/RIGHT walks all 23 and wraps.
- Each style's frame time stays light (timed in the check, against PLASMA).

## Out of scope

- New audio analysis (they use the waveform and its loudness, as the others do).
- Per-style settings.

## Help

- SETTINGS > VISUALIZER's and VI's help: more styles to step through.
- Manual: the new styles in the visualizer list.
