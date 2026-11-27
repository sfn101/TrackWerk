# Help and lessons

Agreed 2026-11-27. Combines roadmap item 17 (help pass), the "Help pass, extended" idea and the "Lessons course" idea
into one item in three phases, built and merged in order. The user waived the grill-me session for the lessons: their
content is written from experience with how this family of trackers is taught (M8, Polyend Tracker, LSDJ,
LittleGPTracker, picoTracker tutorials and manuals) and checked against what TrackWerk really does.

## Goal

Someone who has never used a tracker opens TrackWerk, follows the course, and ends with a finished, mixed and exported
track they're proud of, having learned every main screen on the way. Every help line they read on the way is true.

## Phase A: the help pass

Everything in [help-pass.md](help-pass.md) (its gaps, the complete manual, the selectable LATENCY row, its tests and
roadmap ideas), plus the help of everything built since it was written:

- Check the entries already added for LIMITER, INTERP, INTERP DEF / INTERP MAX, the oversampled DRIVE line and the
  recorder on Windows against the code.
- The LATENCY help names CLEAN's 1.5 ms look-ahead (it exists now).
- `quick-start-guide.md` points new users to the course.

Merged and CI-checked on its own before phase B.

## Phase B: easier-to-read help

- **Less crowded:** a blank line between the summary and the body, and between the body and the keys, where the full
  overlay has room; the compact panel unchanged. Pinned by the existing help-box checks.
- **Real units in help lines,** while every grid stays hex: a help entry may add the value in units after the hex, for
  example `VOL 80 = -6 dB`, `DEL 40 = 25%`, `LAT 03 = 3 ticks`. Only where the conversion is exact; a check compares
  each printed unit with the code's own conversion.
- **Out of this phase:** the always-on key bar (its own roadmap item: it changes every screen's layout).

Merged and CI-checked on its own before phase C.

## Phase C: the lessons course

### How it works

- **Built from First Beat** (`ui/first_beat.h`). Its card, its "a step ticks off only when you really do it" rule
  (judged by the song, the screen and the transport, never a timer) and its SELECT / START / L controls become a
  general lesson runner. First Beat becomes lesson 1, unchanged for the player.
- **Opened from help on SONG:** SELECT, then START opens the **LESSONS** list: the ten lessons, each ticked once done.
  A opens one. Progress (which lessons are done) is saved in `settings.json`, never in a song.
- **One song across the course.** Every lesson adds to the same song, `LESSONS`, saved in `Projects/`. A lesson opened
  out of order, or after the song was changed beyond recognition, offers **START FROM HERE**: it builds that lesson's
  starting point (the song as the earlier lessons leave it) in code, so nothing extra ships and it can never go stale.
  The player's own song is never overwritten: the built one is saved as `LESSONS2`, `LESSONS3`… like First Beat's names.
- **Sounds:** only what ships with the app: the Studio synths, the factory instrument presets
  (`factory/Presets/Instruments`, PocketStudio MegaPack), Drumlab, and the player's own voice in lesson 8. No network.
- **No wall of text in the app (decided 2026-11-27).** The app shows only the doing: per step a title, one line of
  what to do, one line of buttons (each at most 44 characters, as First Beat's), and the check that ticks it off.
- **The teaching lives in a guide outside the app:** `source/docs/lessons-en.md`, one chapter per lesson, same numbers
  and step names as the cards. Each chapter has the music idea behind the lesson (the "why"), what to listen for, the
  finished phrases written out as tables to compare with, and "try next" ideas. Today no release ships the manual, so
  phase C adds a `docs\` folder (manual and guide) to the Windows zip; on Android and the handhelds the guide is read
  on the fork's GitHub page, which the README links. The lesson card's last line names its chapter (for example
  `GUIDE: LESSON 3`).
- Each lesson ends on a short **done** card: what you made, in one line, and the guide chapter for the why.
- **The song:** deep house in A minor at 122 BPM: a swung four-on-the-floor beat, a rolling bassline, warm 7th
  chords, a short lead hook, and an intro / groove / break / drop / outro arrangement. A style that sounds finished with
  few parts, shows off every screen, and is well taught on trackers.

### The lessons

Each lesson's steps are listed with what they ask; the exact card texts are written in the build. The WHY lines
below become each guide chapter's opening, not text in the app. Commands are named
as the app names them (`ui/fx_helper.h`).

1. **First beat** (today's First Beat, 10 steps, unchanged): a chain, a phrase, a Studio sound, four notes, play,
   change, delete, save, load.
2. **Drums that groove**
   - Load a kick, snare, closed hat and open hat from the factory presets into instruments 01–04 (INSTRUMENT, LOAD).
   - Kick on every 4th step (four on the floor), snare on steps 5 and 13, hats on the off-beats, each its own track.
   - Accents: lower some hats with the VOL column.
   - A hat roll with `RPT` (retrigger) on the last step.
   - Swing: PROJECT `SWING` to 56–58 and hear the shuffle.
   - Drumlab as the fast way: generate a pattern, compare, keep the one you like.
   - WHY: what makes a beat feel human (accents, swing, space).
3. **Bassline**
   - A bass preset on instrument 05; track 5.
   - SCALE: A minor on track 5, so every note fits.
   - An off-beat bassline over two phrases (A, C, E, G), octave jumps for movement.
   - `PSL` (pitch slide) into one note; note length with `KIL`.
   - WHY: bass and kick share the low end; the off-beat leaves the kick room.
4. **Chords and a hook**
   - A pad or keys preset on instrument 06; chords Am7 – Fmaj7 – Cmaj7 – G with `CHD` (chord on this note).
   - Voicing: move a chord's root with the chain's transpose, not new phrases.
   - A four-note lead hook on instrument 07, `ARP` on one note for sparkle.
   - WHY: a progression, voice leading, why repetition makes a hook.
5. **Arrangement**
   - Chains for intro, groove, break, drop, outro; song rows that build them up.
   - Transpose a chain instead of copying phrases.
   - A break: drums out, chords alone; `HOP` to end a phrase early for a fill.
   - Variation without copies: `CHA` (chance) and `NTH` (only on some passes) on hats and a fill.
   - WHY: tension and release; changing something every 8 bars.
6. **Movement with commands**
   - Filter sweep on the chords: `LPF`, then `AUS` / `AUF` to open it over 4 bars.
   - Echoes: `DEL` (delay send) on the hook's last note, `REV` (reverb send) on the snare.
   - Late notes with `LAT`, pitch drops with `PBN`, vibrato with `PVB`.
   - A TABLE for a repeating movement: a volume pump on the pad, started with `TBL`.
   - WHY: a loop stays alive when something keeps moving.
7. **Shaping sounds**
   - INSTRUMENT: filter type, cutoff and resonance; DRIVE on the bass (clean on INTERP CUBIC).
   - MODS: a volume envelope that gives the pad a slow attack, and an LFO on its filter.
   - A Studio synth for the lead: pick a model, change its macros, APPLY.
   - WHY: shaping a sound so it sits in the mix instead of fighting it.
8. **Sampling**
   - Record a short vocal phrase (Studio RECORD, any mic) or resample the chords (Internal master).
   - SAMPLE EDITOR: trim, normalize, slice; play slices with `SLI`.
   - Resample a bar of drums into a new instrument (A, A on a SONG selection).
   - WHY: sampling turns any sound into an instrument; resampling frees tracks and CPU.
9. **Mixing**
   - MIXER: levels with the faders, kick and bass loudest, then the rest under them.
   - Pan the hats and the hook a little apart.
   - EFFECTS: one reverb and one delay for everything, set to the tempo; sends per instrument.
   - EQ: cut the lows from pad and hats, so the kick and bass are clear.
   - WHY: space in a mix comes from cutting, not boosting.
10. **Finishing**
    - PROJECT: LIMITER CLEAN, INTERP CUBIC or better; why both.
    - Check the master meter: loud but not hitting the ceiling all the time.
    - EXPORT MIX, then STEMS; find the files in `Renders/`.
    - COLLECT, so the song and its samples travel together.
    - WHY: what "finished" means: a song that plays the same anywhere.

### Accuracy

- Every step is written against the app as built, not against the manual: each names the real screen, row and button.
- **A host check walks every lesson by machine:** it plays each step's actions through the input dispatcher and
  requires the step to tick off, then saves and reloads the song. A later change that breaks a lesson fails CI.
- Each lesson's starting point is checked to build, load and play.
- The finished song from lesson 10's starting point is rendered once in the check and must not be silent or clip.

## Platform effects

- Shared UI code only; every platform gets the course. The card and the WHY page fit 4:3 and wide mode, and the
  handhelds' screens.
- Lesson 8's recording step offers resampling instead where there's no input (some handhelds).
- No song-format change. `settings.json` gains the lesson progress.

## Out of scope

- Videos, pictures or sound files in the course (the guide is text and tables).
- Translations.
- The always-on key bar.
- Lessons for MIDI, PERFORM and the theme editor (later lessons can add them).

## Help

This item is the help. Phase C adds the LESSONS list's own SELECT help, the guide `lessons-en.md`, and a short manual
section pointing to it and saying how progress is kept.
