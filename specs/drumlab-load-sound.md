# Drumlab: load a lane's sound

Agreed 2026-11-25 with the user, after Drumlab manual sequencing (drumlab-manual.md).

## Goal

Pick a lane's sound without leaving Drumlab by hand: today a lane's SLOT only points at an instrument that must
already be loaded in INSTRUMENT.

## Control

**L + A** on a lane of the SOURCES page (any column, outside Step Edit). L+A is free everywhere in the Studio;
L is already Drumlab's extra key (pages, preview). A alone, A+arrows and L+arrows keep what they do.

- **The lane's instrument already has a sound** (sample, synth or SoundFont): the Studio closes and the INSTRUMENT
  screen opens on that number, to tweak it or load a different source from its LOAD row.
- **The instrument is empty:** the INSTRUMENT screen opens on that number with the sample browser on top (the same
  browser as INSTRUMENT > LOAD: samples, synths, presets, SoundFonts). Picking a file loads it into that number;
  either way (loaded or B) the browser closes onto that INSTRUMENT screen.
- **The lane is `--`:** Drumlab takes the first instrument number with nothing loaded that no other lane points at,
  sets the lane's SLOT to it, and opens the browser as above. With no free number, nothing opens and the status says
  so.
- **On the GROOVE page or the buttons:** nothing.
- Preview and grid auditions stop first, as when the Studio closes.

Back to Drumlab: reopen the Studio as usual; it remembers the Drumlab tab, page and lane.

## Files and platforms

No file-format change. Loading is INSTRUMENT's own path, so it works on every platform the browser does.

## Tests

- L+A on a loaded lane asks for INSTRUMENT on that number without the browser.
- L+A on an empty instrument asks for the browser on that number.
- L+A on `--` picks the first free number not used by another lane and sets the SLOT to it; none free: no request.
- L+A on GROOVE does nothing; A's release after L+A does not open Step Edit.

## Out of scope

Coming back to Drumlab automatically; loading several lanes in one go.

## Help

- Studio help (`studio_help.cpp`): the Drumlab key list and topic page.
- Drumlab's own help overlay and footer hint.
- Manual: the Drumlab key table.
