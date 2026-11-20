# Refuse files from a newer TrackWerk

Agreed 2026-11-20 after a grill-me session.

## Goal

An older TrackWerk must never open a file whose new data it would silently drop, and then lose that data on SAVE. Today the
song loader reads `version` and never refuses a higher one (`songcore/project_io.h`). Old files keep opening as before: an
app opens older formats, never newer ones.

## The two numbers

- Every versioned file carries:
  - `version`: the format that wrote it.
  - `minReader`: the oldest format version that can open it safely.
- **The check:**
  - A file is refused when its `minReader` is higher than the format this app supports.
  - A file without `minReader` uses its `version` instead.
  - A missing `version` means 0: an old file, which loads and upgrades as today.
- A `version` or `minReader` that is not a whole number from 0 up to a sane limit counts as damage. Songs go through the
  existing damaged-song refusal and repair (`project_damages()`); other files are refused as unreadable.
- **The rule for every later format change** (written here so later specs follow it):
  - Raise `minReader` whenever losing the new data would change how the file sounds, plays or is arranged: notes, commands,
    instruments, mixing, tracks.
  - Raise only `version` for data whose loss is harmless, such as view state or a remembered cursor.
  - An older app never carries data it doesn't know through SAVE (no round-tripping).
- Every versioned file also records the app version that saved it: `"app": "<versionName>"`.
- **Starting numbers:** everything is `version 1, minReader 1`. This change does not alter file contents. The first later
  item that changes a format raises the numbers.

## Which files

| File | Today | After |
|---|---|---|
| Songs `.ptp`, and the autosave (a song like any other) | `version` 1, never refused | Two numbers, refused when too new |
| Saved instruments | `version` 1, never checked | Two numbers, refused when too new |
| Scales `.pts` | no version | Two numbers, refused when too new |
| Grooves `.ptg` | no version | Two numbers, refused when too new |
| Studio FX / Master presets | `version` 1, any other number refused | Older loads; refused only when too new |
| Sample Granular presets | `version` 1, any other number refused | Older loads; refused only when too new |
| `settings.json` | no version | `version` for upgrades only; never refused (see below) |
| Themes | no version | Unchanged until a roadmap item changes them |

**settings.json:**
- The app rewrites it on its own, so refusing it would reset the user's settings to defaults and then save over the file.
- Instead, it keeps every key it does not know: on save it writes its own values over the original object, so settings
  that only a newer app knows survive an older app.
- `version` is used only to upgrade older settings.

## What the user sees

- **A refused file is never changed, renamed or deleted.**
- **Message:**
  - `MADE WITH TRACKWERK <app> - UPDATE TO OPEN` when the file records the app version.
  - `MADE WITH A NEWER TRACKWERK` when it does not.
- **Song:** the open song stays as it was. The same applies to LOAD, to the startup autosave prompt, and to a song opened from
  outside (command line, Android).
- **Instrument, scale, groove or preset:** nothing is loaded and the slot stays as it was.
- **Autosave:** no special handling.
  - A newer autosave is refused like any song and may later be overwritten by this app's own autosave.
  - That case is accepted: it only happens when going back from a newer build.

## Platforms

Shared code only (`songcore/project_io.h`, `native/ui`), so every platform gets it. M8 and later imports are unaffected:
they are read into a copy and never versioned this way.

## Tests

- **Damaged-file fixtures** (`tests/corrupt-files/`): a song, an instrument, a scale, a groove and a Studio preset with
  `minReader` 99.
  - Each is refused with the message.
  - The open state is unchanged and the file is untouched.
- A song with `version` 99 and `minReader` 1 opens.
- A broken `version` in a song is reported as damage.
- **settings.json:** an unknown key survives a load and save.
- Old files: the existing song goldens and fixtures still load unchanged.

## Out of scope

- Marking newer files in the file browser (it would read every file while browsing).
- An "open anyway" or read-only mode.
- Moving a newer autosave aside.
- Carrying unknown data through SAVE.

## Help

- **Manual:** the refusal message, in the LOAD (PROJECT) and File Management sections.
- **Manual:** a short note in File Management that files record the TrackWerk version that saved them.
- No new controls, so no new SELECT help entries. An existing entry that describes LOAD gets the message if it lists LOAD's
  messages.
