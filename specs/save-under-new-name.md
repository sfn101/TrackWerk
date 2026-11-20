# Save under a new name

Agreed 2026-11-20 after a grill-me session.

## Goal

No save replaces an existing file without asking, and when a name is taken there is always a way to keep both. Before
this change:
- Instruments, themes, scales, grooves, reverb and delay overwrote a file of the same name silently.
- Songs asked `SONG EXISTS - OVERWRITE?`, but B only cancelled.

## The rule for every typed-name save

**Covers:** songs `.ptp`, instruments `.pti`, themes `.ptt`, scales `.pts`, grooves `.ptg`, reverb `.ptr`, delay `.ptd`,
and chords once the chord editor exists.

1. When the target file exists, the app asks `<THING> EXISTS - OVERWRITE?`, using the existing confirm dialog:
   - `SONG`, `INSTRUMENT`, `THEME`, `SCALE`, `GROOVE`, `REVERB` or `DELAY`.
   - The existing song-only `SAVE_OVERWRITE` becomes the shared case.
2. **A** overwrites.
3. **B** opens the on-screen keyboard:
   - It starts with `<NAME>-COPY` typed in, cursor at the end.
   - If that name is taken, it suggests `<NAME>-COPY-2`, `-COPY-3` and so on, using the first free number.
   - The numbering fills in the suggestion only.
4. **Applying the keyboard** saves under the typed name. If the name you typed is also taken, the app asks step 1 again;
   it never numbers silently.
5. **Cancelling the keyboard** returns to where SAVE was pressed: nothing saved, nothing changed.

**The copy becomes current**, for every type. Only names change, never the sound:

| Type | What changes |
|---|---|
| Song | NAME, and the file the next SAVE writes to (`projectPath`); the old file is untouched |
| Instrument | the instrument's NAME in its slot |
| Scale / groove | the slot's name |
| Reverb / delay | the preset name shown |
| Theme | the new file becomes the active theme, remembered in settings |

**Presets always ask when the name is taken**, including when you re-save your own preset under its own name. Presets do
not remember which file they came from, so one extra A press is the price.

**Songs:** saving over the open song's own file never asks, as today. A song repaired but not yet saved still asks, as
today (`repairedUnsaved`).

## COPY (save as) on PROJECT

- The PROJECT row becomes four columns: `SAVE  COPY  LOAD  NEW`. (Planned as `SAVE AS`, which is too wide for the row; changed to COPY on 2026-11-20.)
- COPY opens the same keyboard with `<NAME>-COPY` (numbered if taken), then follows steps 4 and 5.
- No rows move.

## Also covered

- **FILE RENAME** never overwrites. A taken name shows `NAME TAKEN` and the keyboard reopens with the typed text. Before,
  it showed `RENAME FAILED` and closed.
- **The locked-file song backup** (`<name>-backup.ptp`, from audit batch 2) is numbered `-backup-2`, `-backup-3` and so
  on, and never replaces an older backup.
- **Bug fix:** a typed instrument name is passed through `safe_file_name` before it becomes a path. Before, it went into
  the path as typed (`qwerty.cpp`).

## Unchanged

- SAVE TEMPLATE (`template.ptp`).
- Studio's custom FX/Master slots: choosing the slot is the decision.
- RESAMPLE, sample SAVE AS and Sample Granular presets, which number their files.
- The sample editor's OVERWRITE button.
- COLLECT, which already asks.

## Implementation notes

- The keyboard is cleared before it applies (`qwerty.cpp`), so a typed-name save that must ask keeps the pending path,
  type and text in state until the dialog answers.
- Existence checks go through `FileSystem`, so Android's SAF storage works the same way.

## Platforms

Shared UI code only. Every platform gets it.

## Tests

A host check covers:
- the overwrite question for each type: A overwrites, and B suggests `-COPY` and then `-COPY-2`;
- a typed taken name asking again;
- cancel changing nothing;
- the copy becoming current;
- COPY (save as);
- rename onto a taken name;
- backup numbering;
- an instrument name with `/` saving safely.

## Help

- SELECT help for the PROJECT row's COPY (`help_text.h`), and for each save whose help mentions overwriting.
- Manual: the PROJECT section (COPY, the overwrite question), File Management (rename `NAME TAKEN`, the overwrite
  question for presets), and the locked-file backup numbering.
