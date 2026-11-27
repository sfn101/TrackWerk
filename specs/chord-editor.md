# Chord editor

Agreed 2026-11-19, detailed 2026-11-20 after a grill-me session.

## Goal

The song's 16 chords become editable song data. Today they are 16 fixed shapes (`songcore/chords.h` `CHORDS`). `CHD xy`
picks a chord (x) and one of 16 voicings (y).

## The CHORD screen

The CHORD screen is built like SCALE and sits between SCALE and GROOVE in the screen map (R+D-pad).

| Row | What it does |
|---|---|
| SLOT 00-0F | Which of the song's 16 chords is edited |
| NAME | A+LEFT/RIGHT cycles the built-in presets; editing a note adds `*` (`MINOR 7*`). A opens the keyboard to type a name of up to 12 characters, which `CHD`'s hint and the chord pen's list show |
| NOTE 1-4 | Each an interval above the root, shown as a number and a name (`+3  b3`, `+7  5`, `+10  b7`, `+14  9`). A+UP/DOWN moves a semitone, A+LEFT/RIGHT an octave, range 0 to +24. NOTE 1 is always the root (+0). A row can be `--` (unused) |
| VOICING | Preview only: the voicing START plays. Nothing is stored |
| SAVE / LOAD | `.ptc` chord files in `Chords/`, with save-under-new-name.md and refuse-newer-files.md |

- **Triads:** a chord with 3 used notes doubles its root an octave up as its fourth note, as today, so all 16 voicings
  still work.
- **Hearing a chord:** START plays the chord on the current instrument at C-4 in the VOICING row's layout. Any button
  stops it.
- **The header** shows `USED ON n STEPS` (or `NOT USED`), so you can see what an edit affects: every `CHD` using that
  slot.

## Presets

One built-in chord list for the whole app:
- today's 16 shapes;
- the shapes only Plaits' chord engine has: octave, fifth, 6/9, spread major 10th, dominant 7♭9, half-diminished, fully
  diminished, as 4-note shapes.

## Songs

- A new song, and an old song with no chord data, starts with today's 16 shapes in today's order. Old songs sound
  exactly the same.
- Each chord's notes are stored as a **list**, so a later item can allow more than 4 without a format change. Only the
  screen enforces 4 (`CHORD_MAX_NOTES`).
- **File format:** song chords raise the song's `version` and `minReader`. An older app would play the fixed shapes
  instead.

## Who reads the song's chords

- `CHD`, through `chord_voicing` / `chord_notes`, which now take the song's chord table instead of the constant.
- HARMONY.
- Later, Chords on synths.

## Tests

- **Old songs:** they load today's shapes, and every `CHD` byte gives bit-identical notes to the old constant table.
- **Editing:**
  - an edited chord changes its `CHD` notes;
  - a 3-note chord doubles its root;
  - all voicings work on edited chords.
- **Files and rules:**
  - `.ptc` save and load;
  - the usage count;
  - the new-name and refuse-newer rules for chord files and for songs with chords.

## Out of scope

- More than 4 notes.
- Chords on synths (the next item).
- Per-step chord editing outside `CHD`.

## Help

- SELECT help for every CHORD row (`help_text.h`).
- `CHD`'s hint (`fx_helper.h`) shows the chord's name.
- Manual: a CHORD screen section, and the screen map updated.
