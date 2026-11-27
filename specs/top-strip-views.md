# Top strip views

Agreed 2026-11-27 (from the help phase B2 discussion). Not started.

## Goal

The strip at the top of the screen shows the oscilloscope, or on PHRASE the NOTES and VELOCITY overview. Each screen
gets views of its own there, so what a screen edits can also be seen: one item, built and merged step by step.

## How it works on every screen

- **SELECT + LEFT/RIGHT** cycles the strip's views, as on PHRASE today: AUDIO (the oscilloscope, always first) and that
  screen's own views. Where a screen already uses SELECT + LEFT/RIGHT, the spec of that step picks another key and says
  so in the help and the key bar.
- **The choice is remembered per screen** while the app runs, and AUDIO is the default.
- **A view follows the cursor and what plays:** the cursor's step or row is marked, and the playhead moves over it.
- **Drawing only:** a view never changes the song. Editing stays on the screen's own cells, and the view redraws at once.
- **Same look:** the theme's strip colours, in 4:3 and wide mode; FLAT visualizer style hides nothing here.

## Steps (in this order)

1. **PHRASE: FX and chords.** Which steps carry commands (a mark per FX slot), and `CHD` chords drawn as their stacked
   notes in the NOTES view.
2. **PHRASE: note lengths.** Each note drawn as a bar that runs until it stops: the next note, `OFF`, `KIL`, or the end.
3. **CHAIN: the chain's melody.** The notes of every phrase in the chain side by side, each shifted by its row's
   transpose, the cursor's row marked.
4. **SONG: the song map.** Where each of the 8 tracks plays down the whole song (filled cells as bars), the parts split
   by empty rows, and the playheads.
5. **INSTRUMENT: the sample.** The waveform with start, end and loop markers (and slices), which move as their rows are
   edited.
6. **MODS: the shapes.** The envelope or LFO of the cursor's slot as a curve, with a dot riding it while a note plays.
7. **TABLE: the columns as graphs.** The table's volume and pitch (and an FX value column) drawn over its 16 rows.
8. **SCALE: the keyboard.** A small piano octave lighting the scale's notes, the key's root marked.
9. **MIXER: the limiter at work.** How much LIMITER CLEAN or CLASSIC is lowering the mix, as a moving bar and a held
   peak, beside the master level.
10. **GROOVE: the groove on your notes (to confirm with the user).** The step timings are already on the GROOVE screen;
    the strip shows instead the current phrase's notes placed where the groove moves them, against a straight grid.

## Platform and file effects

- Shared UI code only. No song-format change; the chosen view per screen is not saved.
- Drawing cost stays small on handhelds: each view draws at most a few hundred rectangles, and only while shown.

## Out of scope

- Editing from the strip (it is a view).
- Studio's synth visuals (their own roadmap idea).

## Help

Each step adds its view to that screen's SELECT help and the key bar, and to the manual's screen section.
