# Merge upstream 1.3.0 into the fork

Agreed 2026-11-20. Highest priority: everything we already have must work on top of upstream's newest release before any new feature (wide mode included) is finished.

## Rules

- **Merge, never rebase.** `git merge origin/main` into `main`, so we can keep pulling every future upstream release the same way.
- **Their code is the base.** Where both sides built the same thing, keep upstream's version and add back only what ours does extra. Nothing we built or planned is lost.
- **Performance, stability and quality first.** If combining two versions would cost any of them, stop and ask.

## Where both sides built the same thing

| Area | Keep from upstream | Add back from ours |
|---|---|---|
| Damaged-file checks (WAV, SF2, SF2 trim, song values) | their checks (`common/riff_bounds.h`, `project_io.h`) | any case ours catches that theirs doesn't; our corrupt-file tests |
| Newer-version files | — (they have none) | our format numbers, `minReader` and the refusal message |
| Repair a damaged song | their silent repair as the base | our "damaged, repair a copy?" prompt on top |
| LOAD with unsaved work | their LOAD PROJECT? prompt | nothing, if it covers ours |
| SAVE over another song | their REPLACE / NEW COPY / CANCEL prompt, cursor on CANCEL | our COPY button on the PROJECT row (save under a new name at any time) |
| Durable save | their temp file, fsync, rename, folder sync | our Windows flush (`_commit`), which theirs skips |
| COLLECT name clashes | their version | nothing, if it covers ours |
| Windows CI | theirs | keep ours only if it checks something theirs doesn't |

## Take from upstream as-is

- **PRB** (row chance, code 0x40) replaces our planned PRO probability command.
- **SAVE backups:** the last 5 versions in `Backups/`.
- **Preset inbox:** `Presets/Inbox`.
- **EXPORT > MIDI**, then the roadmap's MIDI export item adds only what it lacks.
- All other 1.3.0 features: the new synths (DUB SIREN, ACID BOX, REESE BASS, FROGGY, CROAKER), the XY pad page, the per-family synth sheets, the AUDIO DRIVERS overlay, input monitoring (Linux) and the new handheld packages.

## Our features to check after the merge

All of these must still work, on Windows by hand and on Linux/Android through CI:

- latency row
- VI visualizer
- repair on request
- refuse newer files
- save safety, including the Windows flush
- audit fixes 13, 14, 15 and 20 (still missing upstream)
- Studio SOURCE on Windows and the YouTube picker
- COPY
- sharp scaling
- the name keyboard's first-B rule

Their settings rows and ours (LATENCY, SCALING) must all be reachable, and the navigation check must be updated.

## Expected conflicts

33 blocks in 17 files, from `git merge-tree` on 2026-11-20:

- **Mostly in the file-safety code** above.
- **Shared lists:** tests in `CMakeLists.txt` and `build.yml`, plus the settings and confirm lists, `qwerty_keyboard.h`, `help_text.h`, `manual-en.md` and `README.md`.
- **`shell/main.cpp`:** their monitor hook against our Windows SOURCE guard.
- **`shell/app.cpp`:** only when wide mode is merged. Keep both: our two-finger toggle, then their XY pad touch.

## Done when

- `main` contains upstream 1.3.0 and every feature above.
- The Windows build is tested by hand.
- CI is green on Linux and Android.
- The roadmap is updated: MIDI export reduced to extras, PRO dropped, item 10 (wide mode) next.

## Help

Correct any help or manual text where the merged behaviour differs from what either side described (the SAVE prompt and COPY together, backups, PRB).
