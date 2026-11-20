# Help pass

Agreed 2026-11-18, detailed 2026-11-20 after a grill-me session.

## Goal

Every feature this fork has added has its help, and help that no longer matches the app is corrected. Every line is
checked against what the code does now, not against older documents.

## Where

- `source/native/ui/help_text.h` (SELECT help)
- `fx_helper.h` (effect commands)
- `studio_help.cpp` (Studio)
- `source/docs/manual-en.md`
- `source/docs/quick-start-guide.md`
- `shell/windows/README.txt`

## Gaps to fill

- The VI screen and its VISUALIZER row. Check the existing entries.
- Audit batch 1's boxes and messages: LOAD discard, SAVE overwrite, damaged-file refusals and repair.
- Audit batches 2 and 3's messages:
  - `FILE LOCKED: SAVED AS <name>-backup.ptp` and the read-only fallbacks;
  - COLLECT's `_2`, `_3` names;
  - the other messages those specs added.
- The name keyboard's first-B rule.
- SONG's help gains "A,A on a selection resamples it".
- PHRASE's help says B+LEFT/RIGHT goes to the next track's phrase (under NAV = SONG).
- **The latency row in SETTINGS becomes selectable.**
  - The cursor can land on it, and its value stays greyed (information only).
  - Its SELECT help says:
    - what the number means: keypress to sound, at best;
    - what it includes, among them the CLEAN limiter's 1.5 ms look-ahead once that exists;
    - that the audio buffer size is the main lever.
- Studio SOURCE (below).
- The Windows README: SOURCE, `tools\`, the log. Check it against what the build does.

## The manual becomes complete

- **No upstream pointer.** The manual stops pointing to upstream's illustrated handbook; the line under its title goes.
  Everything from the handbook (`docs/site/`) that still matches our app is written into `manual-en.md` as text,
  including the Studio. Anything that no longer matches is left out, or rewritten to match.
- **A chapter on what this fork adds or changes,** including a **Studio SOURCE** section: the providers, YouTube on/off,
  the link field, the 15-minute rule, previews and the cache, and where takes are saved.
- **Text only.** No pictures are copied from upstream. Our own illustrated guide comes later from our own canvas
  captures (roadmap idea).
- **Upstream's website (`docs/site/`) is not edited,** so merges from upstream stay clean.

## Roadmap ideas added by this spec

- **Our illustrated guide:** screenshots made with our canvas capture tool (`tools/showcase`), including wide mode, for
  a visual guide of our own.
- **Own identity:** a name and an icon (Windows `.ico`, Android adaptive and monochrome icons, Linux icons, favicon), for
  when the fork goes independent.

## Tests

- The existing help checks still pass, and new entries fit their boxes (the help panel's line limits).
- A check that the latency row can be selected and is not editable.

## Out of scope

- Pictures in the manual.
- Changes to `docs/site/`.
- Help for features not built yet. Each of those ships its own help, as CLAUDE.md requires.

## Help

This item is the help.
