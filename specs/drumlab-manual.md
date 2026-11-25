# Drumlab: manual sequencing and layout

Agreed 2026-11-19, completed 2026-11-20 after a grill-me session.

## Goal

Drumlab (Studio tab, `ui/drumlab.cpp`) becomes a drum sequencer you can fill by hand. Today it needs Generate first and at
least 5 lanes with sounds.

## Controls

- **CLEAR**, next to GENERATE, empties the grid, so Step Edit works without Generate.
  - Locked lanes are kept, as Generate already respects locks.
  - If the grid holds hits that were never applied, CLEAR asks `CLEAR PATTERN?` (A yes, B no). An empty or already applied
    grid clears without asking.
- **Empty lanes are allowed.**
  - A SLOT can be `--` (no instrument).
  - A lane with no loaded sound is silent, and Generate, Preview and Apply skip it instead of being blocked by it (today
    `ready()` refuses).
  - The active-lane minimum drops from 5 to 1 (today `activeLanes < 5` is invalid).
- **Two pages:**
  - **SOURCES:** the lanes (slot, lock, rhythm fields).
  - **GROOVE:** style, lanes, steps, density, variation, swing, humanize, fill, seed, map X/Y/chaos.
  - L+LEFT/RIGHT switches pages outside Step Edit.
  - GENERATE / PREVIEW / APPLY sit on both pages.
  - L or R alone still turns the 16-step bar.
- **Step Edit:**
  - L+DOWN loops the preview (instead of A+DOWN). START stays the song's play.
  - A tapped toggles a hit.
  - A+UP/DOWN sets the hit's velocity.
  - A+LEFT/RIGHT sets its timing nudge (0..11 ticks).
  - Velocity and nudge are already stored and written by Apply.
- **Key checks (2026-11-19):**
  - L+arrows are unused in the Studio. L+SELECT is the Punch chord, and L+R+START is live resample.
  - The Studio already swallows a shoulder's release after a combo.

## Apply

- Unchanged: it appends at the end of the song and never touches existing music. Lane N goes to track N, using empty,
  unused chains and phrases.
- A lane with no sound, or with no hits, writes nothing. Its track stays empty at the new song row, and no chain or
  phrase is used up.
- Applying again appends again. Earlier applies stay in the song and are deleted there if not wanted.

## Wide mode

Drumlab is centred, like the rest of the Studio (wide-mode.md).

## Tests

- CLEAR keeps locked lanes. It asks only with unapplied hits.
- Generate, Preview and Apply with empty lanes, and with a single active lane.
- Apply writes no chain for silent lanes.
- Step Edit: A toggles, A+UP/DOWN changes velocity, A+LEFT/RIGHT changes nudge, L+DOWN loops.
- L+LEFT/RIGHT switches pages and is ignored in Step Edit.

## Out of scope

Per-step chance (`CHA`), ratchets (`RPT`) and pitch (`PIT`). They are a later item of their own.

Packing several lanes onto one track (decided 2026-11-25: no). Apply keeps one track per lane: a track plays one
note at a time, so lanes sharing a track would cut each other's tails (long kicks, snare rooms).

## Help

- Drumlab's Studio help pages (`studio_help.cpp`): CLEAR, the two pages, the Step Edit keys and empty lanes.
- Manual: the Drumlab section, or a new one if there is none.
