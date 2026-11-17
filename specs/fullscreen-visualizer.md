# Full-screen visualizer (VI screen)

Agreed 2026-11-17.

## Goal

A screen that shows the visualizer (the bar normally at the top or bottom of the screen) across the whole main area, moving with the music like the bar does. The side panel (status, tempo, tracks, navigation map) stays, as on every screen.

## Getting there

- The code calls the S/C/P/I/T places "screens" (`ScreenType`, `ui/screen.h`). They form a 2D map; R + d-pad moves between them.
- The new screen sits **left of SONG** on the main row: `V S C P I T`.
- R+LEFT on SONG goes to VI; R+RIGHT on VI goes back to SONG. R+UP and R+DOWN do nothing on VI (nothing above or below it).
- Label: **V** on the map (one letter, like every main-row screen) and **VISUALIZER** in the arrival banner. MIXER is unchanged: the map draws it as `MX`, below the main row, so no `V` is ever shown twice.
- The map shows five columns at a time. On every screen but VI it shows `S C P I T` with a small `<` at its left edge (something further left); on VI it slides one column: `V S C P I` with a `>` at its right edge. The same arrows mark any future screen beyond either edge.
- The app still opens on SONG.

## Screen

- The main area (everything but the side panel) shows only the current visualizer style. No song name, tempo or other overlay inside it; the side panel is drawn as on every screen.
- When the style changes, its name fades in for about a second, like the screen-name banner on arrival.
- Each style is **redrawn for the full area, not stretched**: Spectrum gets tall bars, Scope a full-height wave, Octa eight full-size lanes, and so on. The drawing code takes the area size as input instead of assuming the 620×70 strip. Styles that look poor at full size get tuned during the build; none are removed.
- On screens that aren't 4:3 (wide monitors, phones) the platform keeps the 4:3 area and adds black bars, as it does for every screen today. This works the same on Windows, Linux, Android and the handhelds, because the screen lives in the shared UI code.

## Controls on VI

| Input | Action |
|---|---|
| A+RIGHT / A+LEFT | Next / previous style, wrapping around (9 styles: SCOPE, FLAT, OCTA, OCTA FULL, SPECTRUM, SPECTRUM PEAKS, COPPER, STARFIELD, PLASMA) |
| Play/stop | Same as every screen |
| R+RIGHT | Back to SONG |
| Everything else | Nothing (no accidental edits) |

## Behaviour

- The style is the **same setting** as Settings → VISUALIZER (`Theme::visualizerType`). Changing it on VI changes the bar too, and it is saved like today.
- Same as the bar when stopped: the trace fades out; COPPER, STARFIELD and PLASMA only animate while playing with Settings → Motion on.

## File format

None new. The style is already saved in the settings.

## Out of scope

Filling wide screens edge to edge (on the roadmap). Hiding the side panel on VI. A separate style setting for the full screen. Overlays (song info, track meters).
