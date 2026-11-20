# Wide mode (16:9)

Brainstormed 2026-11-18, agreed 2026-11-20 after a grill-me session. Built after Sharp scaling (roadmap 9).

## Goal

Fill wide screens: RG505, Odin, TrimUI Smart Pro, laptops and tablets. 4:3 stays the default and stays exactly as it is.

## The setting

- SETTINGS gets `SCREEN: 4:3 / 16:9`, with 4:3 the default. It is saved on the device (`settings.json`).
- The change applies on the next start. After changing it, the row shows `RESTART TO APPLY`.
- Why a restart: the canvas size is a compile-time constant today (`DESIGN_W/H`, `canvas.h`). The window and texture are
  made once from it, and every module draws at fixed positions. Switching at startup avoids rebuilding all of that while
  the app runs.

## Canvas and placement

- **The canvas is 854×480 in wide mode.** It keeps the same height, so text stays the same size.
  - `DESIGN_W` becomes a startup value (640 or 854).
  - `DESIGN_H` stays 480.
- **Screens stay anchored and never jump between screens.**
  - The right bar (BPM, note monitor, navigation map) sits at the far right edge: `RIGHT_BAR_X` follows the width.
  - Editors stay where they are on the left, beside the rail. The spare space is always on the editor's right, whichever side the rail is on: rail, editor, spare space, or editor, spare space, rail.
- **The navigation side panel is always shown.** No screen is a true full-screen view.
- **The oscilloscope band at the top stretches to the full width.**

| Screen | Wide mode, first version |
|---|---|
| PHRASE | The other 7 tracks' notes beside the phrase (below) |
| File browser | Stretched and reorganised to show more files at once |
| Sample editor | Stretched and reorganised. The waveform is larger with usable proportions, not thin and wide; the screen may scroll down |
| INSTRUMENT | Beside the rail like the other editors; the space on its right is kept for More mod targets |
| Studio | Centred; the side space is kept for Advanced Synth Studio |
| SONG | Peek: the chain under the cursor (PH, TSP) beside the song, view only. The 16-track item takes the space later |
| CHAIN | Peek: the phrase under the cursor (NOTE, VOL, INST) beside the chain, view only |
| MIXER | Extra space empty. The 16-track item uses it (greyed tracks 9-16 under 8 tracks) |
| Everything else | Extra space empty |

**PHRASE side view:**
- Shows the note (only the note) that each of the other 7 tracks plays on the same step.
- The notes line up row by row with the phrase being edited, are greyed, and show `---` where nothing plays.
- View only: the cursor never enters it.

## Android

- In wide mode the app is locked to landscape, whichever way the phone turns.
- **Touch buttons in wide mode** are drawn over the picture:
  - about 40% transparent, solid while pressed;
  - the button sound and skin settings still apply.
- In 4:3, the touch layouts stay as today: the portrait skin, or the landscape side bars.
- **`TOUCH BUTTONS: AUTO / ALWAYS / OFF`** in SETTINGS, for every mode:

  | Value | Buttons shown |
  |---|---|
  | AUTO | when no controller is connected (today's behaviour) |
  | ALWAYS | even with a controller. Replaces `touchButtonsWithPad` and keeps its saved value |
  | OFF | never |

  - When it is OFF, a two-finger tap anywhere shows the buttons, and another hides them. The tap does nothing in AUTO or
    ALWAYS.
- Android's own navigation bar stays hidden as today (immersive mode, shown by a swipe from the edge).

## Every theme and scene layout

Wide mode works with every theme and every scene layout that exists, including the ones that move parts of the screen
(`scene_layout.h`), without special cases left for later:
- **`railLeft`** (side panel on the left): the panel sits at the far *left* edge. Editors anchor on the right and the extra
  space opens between them, mirroring the default layout.
- **`scopeBottom`** (visualizer band at the bottom): the band stretches to the full width at the bottom, exactly as it does
  at the top.
- The PHRASE side view, the centred screens and the stretched file browser and sample editor all follow the same mirroring.
- **The theme / scene picker's previews** (`scene_picker.cpp`) show the wide layout when wide mode is on.
- Every theme's colours, fonts and surface styles apply to everything new.
- Tests render each scene layout in wide mode. Every built-in theme is drawn at 854×480 without overlaps.

## Desktop (Windows, Linux)

- The window opens at 16:9: the largest whole scale that fits, for example 1708×960 on a 1080p screen.
- It resizes freely. The picture scales to fit and keeps 16:9, with bars where the window has another shape, as in 4:3
  today.
- The layout itself doesn't reflow to other shapes. That would mean redesigning every screen around a flexible layout,
  which this item does not do.

## Rules kept from the brainstorm

- Nothing exists only in wide mode: every control can be reached in 4:3.
- Everything new follows every theme, font and surface style.
- No phone rotation feature beyond the landscape lock.

## Tests

- New exact-pixel reference hashes for the screens at 854×480.
- Every existing 4:3 hash stays identical: 4:3 output must not change by a single pixel.
- A check that `touchButtonsWithPad` reads as ALWAYS.

## Out of scope

- Using the width on SONG and MIXER (the 16-track item).
- Using the width on INSTRUMENT and the Studio (More mod targets, Advanced Synth Studio).
- A layout that adapts to any window shape.
- Switching without a restart.

## Help

- SELECT help for the SETTINGS rows `SCREEN` and `TOUCH BUTTONS` (`help_text.h`).
- The PHRASE help mentions the side view in wide mode.
- Manual: a Wide mode section, with the restart, the landscape lock, the overlay buttons and the two-finger tap.
