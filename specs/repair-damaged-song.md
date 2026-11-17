# Repair a damaged song on request

Agreed 2026-11-17.

## Goal

When a song is refused as damaged, ask whether to repair it. A repairs it and loads it; B leaves it unloaded. Today the song is refused with `LOAD FAILED: FILE DAMAGED`, and the only way to get the work back is to fix the file by hand.

## Which files

- **Songs (`.ptp`) only.** Samples and SoundFonts keep `SAMPLE FAILED: FILE DAMAGED`.
- **Only songs that can be read.** A file that isn't valid JSON, or isn't a JSON object, has nothing to repair: it still says `LOAD FAILED: FILE DAMAGED`, with no offer.
- **Two ways in:** LOAD from the file browser, and RESUME (the crash-recovery file). M8 import is not part of this and keeps working as today.

## The repair box

The existing yes/no confirm box, with a new kind `REPAIR_SONG`. Its title is `FILE DAMAGED - REPAIR?`.

- **A:** repair and load. The status shows `REPAIRED: N FIXES` (`1 FIX` for one).
- **B:** don't load. What happens next depends on the way in (below).

### From LOAD

1. If the current song has unsaved changes, today's box `UNSAVED CHANGES - LOAD ANYWAY?` comes first, unchanged. B cancels the load as it does today.
2. You pick the file in the browser. If it is damaged, the repair box opens.
3. **B:** the status shows `LOAD FAILED: FILE DAMAGED`. The current song stays exactly as it was, unsaved changes included, because nothing replaces it until a repair is chosen.
4. **A:** the song is repaired and loads, marked **unsaved** (the file on disk is not touched). It keeps the damaged file's path.
5. **The first SAVE** of a repaired song asks `SONG EXISTS - OVERWRITE?` (the existing `SAVE_OVERWRITE` box) before writing over the damaged file. A saves, and B cancels. Once saved, it behaves like any song.

### From RESUME

- **ASK setting:** today's `RECOVER WORK?` box comes first. If you press A and the file is damaged, the repair box opens.
- **AUTO setting:** a damaged recovery file goes straight to the repair box instead of being dropped silently.
- **A:** the work comes back repaired and marked unsaved, as recovered work does today, with `REPAIRED: N FIXES`. Recovered work has no file path today, so SAVE works as it does for any recovered song: it saves under the song's name and asks `SONG EXISTS - OVERWRITE?` if a different song is already there.
- **B:** a new empty song starts, and the recovery file is deleted, as it is today whenever you start a new song, so the question doesn't come back at every launch.

## What is repaired

Everything wrong is fixed in one go, with no partial repair. The rules are upstream's, already in `parse_project` / `normalize_project` (`native/songcore/project_io.h`, merged from upstream on 2026-11-17):

| Damage (checked by `project_damage`) | Repair |
|---|---|
| Tempo outside 20..999 | Set to the nearest limit |
| Instrument id outside 0..127 | The id becomes its position in the list |
| Phrase with fewer than 16 steps | Empty steps added at the end |
| Groove step outside -1..255 | Set to the nearest limit |

**The fix count** is one per bad value: a bad tempo plus three short phrases is `REPAIRED: 4 FIXES`. Each fix is written to `trackwerk-log.txt` (for example `repaired: phrase 12 had 9 steps`).

## How it fits the code

- `project_damage()` becomes a list of every problem found, not just the first. Refusing still happens on the first one; the list gives the count and the log lines.
- The song loader takes a "repair" choice. Without it, a damaged song is refused as today. With it, the damage check is skipped, and the existing parse and normalize code does the repair.
- The browser and RESUME paths remember the file and open the repair box instead of showing the failure. A on the box loads again with repair on.
- The audio thread is not involved. Loading already happens off the audio thread, so nothing changes there.

## File format and platforms

- **File format:** none. A repaired song saves as a normal `.ptp`.
- **Platforms:** all of them. The change is in the shared `native/` code (songcore and UI). There is nothing platform-specific.

## Tests

**Automatic, on all three platforms in CI:**
- Each damaged song in `source/tests/corrupt-files/` is repaired, loads, and gives the right fix count.
- A repaired song, once saved, loads again with no damage message.
- The boxes come in the agreed order: unsaved changes first, then repair.
- B on the repair box leaves the current song untouched, unsaved changes included.
- The first SAVE after a repaired LOAD asks before overwriting.
- A damaged recovery file offers the repair under both ASK and AUTO. B starts an empty song and deletes the recovery file.

**By hand, on Windows:**
- Load a damaged song and try A, then B.
- Put a damaged file in place of the recovery file to act as a crash, launch, and try A and B.

## Out of scope

- Repairing samples, SoundFonts or M8 files.
- Choosing which fixes to apply.
- Saving the repaired song under a new name: that is the roadmap's "Save under a free name" item.
- Repairing files that aren't valid JSON.
