# Piano roll (melodic sequencer view)

Agreed 2026-11-19, detailed 2026-11-20 after a grill-me session.

## Goal

A visual way to write one phrase: a melodic sequencer with a piano-roll view. It does what PHRASE does, drawn as pitch
over time, for people who think in melodic shapes and for stamping chords quickly. It stores nothing new; it is another
view of the same phrase data.

**What it isn't:** a DAW piano roll. It has the tracker's 16 steps, and until New Note Action one note at a time per track
(plus chords). The help says so plainly.

## Opening and editing

- **SELECT+A** on PHRASE toggles the roll, on the phrase under the cursor. It returns to PHRASE at the same step.
  - PHRASE is the only way in (no Studio entry).
  - SELECT+A is unused on PHRASE; only the file browser uses it.
- **Live editing:** every change goes straight into the phrase. There's no Apply.
- **Undo and redo:** SELECT+UP and SELECT+DOWN, the same keys as PHRASE. The roll's edits are normal phrase edits, so
  PHRASE's undo sees them too.
- **Buttons only.** The app has no direct touch input (fingers only press the on-screen buttons), so the roll has none.
  It joins touch input if the app gets it as its own feature.

## The grid

- **All 16 steps on one page,** in 4:3 and in wide mode. In 4:3 each step is about 28 px; wide mode just gives more room.
- **24 pitch rows.** UP/DOWN scroll at the edges, and R+UP/DOWN jumps an octave.
- **Two row modes, switched with SELECT+B:**
  - **FOLDED** (the default whenever a scale is active): only the scale's notes are rows, so 24 rows cover about 3½
    octaves of a 7-note scale.
  - **CHROMATIC:** all 12 notes per octave, with rows outside the scale dimmed. A note placed on a dimmed row snaps to the
    nearest scale note (ties go up) and is drawn where it sounds.
  - With no scale set, the grid is always chromatic.
- **Maqam offsets** are shown once that roadmap item exists.
- **The header** shows the phrase, the instrument for new notes, the row mode and the chord pen.

## Keys

| Keys | Action |
|---|---|
| A | Place or remove a note |
| A+UP/DOWN | Velocity (`VOL`) |
| A+LEFT/RIGHT | Length |
| B on a note | Open PHRASE's effect list; pick a command and set its value as on PHRASE |
| L+LEFT/RIGHT | Move to the previous or next command on the note, to read or edit it |
| R+A | The chord pen (below) |
| SELECT+B | FOLDED / CHROMATIC |
| SELECT+UP/DOWN | Undo / redo |
| START | Preview, exactly as on PHRASE |
| SELECT+A | Back to PHRASE |

## What notes write

| Roll | Phrase data |
|---|---|
| Note | NOTE |
| Velocity | `VOL` (the volume column) |
| **Open note** (the default for a newly placed note) | No `OFF`. It rings until the next note, as on PHRASE today, so drums and one-shots are never cut. It is drawn fading out |
| Set length (A+LEFT/RIGHT) | `OFF` on the step where the note ends, unless the next note starts there |
| Nudge | `LAT` |
| Chord | Root note plus `CHD xy` |

- **Instrument:** a new note takes the instrument of the nearest note before it in the phrase, otherwise the current
  instrument. Changing a note's instrument stays on PHRASE.
- **Effect slots:** a command goes in the slot that already holds it, otherwise the first free slot. If all slots hold
  other commands, the change is refused with `STEP FX FULL - EDIT ON PHRASE`. Other commands are never removed or
  overwritten.
- **Overlaps:** a note overlapping a ringing one is refused unless it's a chord from the list. New Note Action adds
  overlaps later, as its roadmap entry describes.

## The chord pen

- **Arming:** R+A opens the chord list: the song's chords (the 16 shapes today, the chord editor's later). UP/DOWN picks
  a chord, LEFT/RIGHT the voicing, and A arms the pen. The header shows `PEN: <chord> (v<n>)`.
- **Drawing with it:** while armed, A on a row places the whole chord from that root. The root gets `CHD` already
  attached, so a progression is drawn by moving and pressing A. A on a chord's notes removes the chord.
- **Disarming:** R+A again opens the list to pick another chord or `PEN OFF`.
- **Changing a chord:** the effect list on a chord's root opens `CHD`'s settings, which change the shape and voicing and
  keep the root.
- **Warning:** if the track's instrument can't play chords (today only samples, SoundFonts and the chord engines can),
  the list shows a warning line. Placing is still allowed.
- **Later, with the chord editor:** the list gains `CREATE A CHORD…`, which opens the editor. The chord saved there is
  armed in the pen on return.

## What is drawn

| Command | Drawn as |
|---|---|
| `CHD` | The chord's real notes, snapped to the scale as `chord_notes()` does, outlined together |
| `LAT` | An offset mark on the note |
| Slides | A line to the target pitch |
| Any other command | A small 3-letter tag on the note |

New commands arrive as tags, and get a picture only when one is worth it.

## Built for later, without extra features now

- Effect slots are read through the `step_fx_*` accessors, so more slots (16 tracks) need no change in the roll.
- The commands the roll writes and draws sit in one table: NNA and future commands are one entry each.
- One overlap rule, which NNA extends.
- Reuses Drumlab's Step Edit grid (drumlab-manual.md): cursor, drawing, preview hooks.

## Tests

- **What edits write:**
  - Each edit writes the right phrase data; undo restores it.
  - An open note writes no `OFF`; a set length writes one.
  - A snapped note lands on the nearest scale note, ties going up.
- **Limits and the chord pen:**
  - The full-slot refusal leaves other commands intact.
  - The pen places root plus `CHD`; editing `CHD` keeps the root.
- **Views:**
  - FOLDED shows only scale rows; SELECT+B switches.
  - SELECT+A in and out returns to the same step.
  - The wide and 4:3 layouts draw without overlaps in every theme and scene layout.

## Out of scope

- Touch drawing.
- Sub-step timing beyond `LAT`.
- Overlapping non-chord notes before NNA.
- Changing a note's instrument in the roll.
- A Studio entry.
- An 8-step zoom.

## Help

- SELECT help for the roll: the keys, the row modes, the chord pen, and "one note at a time per track, plus chords"
  (`help_text.h`).
- PHRASE's help mentions SELECT+A.
- Manual: a Piano roll section.
