# TrackWerk 1.3.0

Small machine. Serious music, and now a frog that sings.

## Added
- **FROGGY** (Studio > Synth > Voices and Frogs): a singing voice that sings the words you type. Type the lyrics on the on-screen keyboard (Lyrics row, A + Right), or step through templates in `Presets/Lyrics` (A + Up/Down); every note sings the next syllable. Four voices (frog, man, woman, child), croak, vibrato, glide, coda, a fixed-syllable option, and a **Mic pitch source** that sings the pitch you hum into the recorder (Linux builds). Spelling-to-sound rules and vowel data from Chimera Vocal Synth by Jameson Hodge (MIT, see `docs/THIRD-PARTY-CHIMERA.md`); the voice itself is our own light formant engine.
- **CROAKER**, a croaking frog; **ACID BOX** and **REESE BASS**, two bass engines; **DUB SIREN**, a sound-system siren. 16 sounds each.
- **The frog stage**: a neon side view that follows the real sound level, hops with every note, flicks its tongue at the firefly (the XY pad's dot), and shows the sung word karaoke-style.
- **XY pad** on every synth, with touch dragging on touch screens.
- **Synth tabs sorted by purpose** (Voices and Frogs, Vintage Chips, Low End and Sirens, Plaits: Macro, Braids: Digital, Rings: Strings, Elements: Modal ...), a one-line description under each new model, and **control sheets** per family: dials, an LCD, a bar graph.
- **EXPORT > MIDI**, **Presets/Inbox**, the **PRB** phrase command, **AUDIO DRIVERS overlay** and **input monitoring** (@ozkaoz), save **backups** (the last five versions) and **durable saves**.

## Changed
- 128 synth models in all. Existing songs and presets are unchanged.

# TrackWerk 1.1.0

Small machine. Serious music.

TrackWerk 1.1 brings a complete visual collection and a deeper sound-design workflow to Android, Brick/KNULLI, both Miyoo Onion profiles and RGB20S/AmberELEC.

- **40 complete Scenes in 10 families**, including Daylight and Hardware; 54 palettes and independent typefaces.
- **Optional motion** for cursor, lists, tabs, controls, playback marks and page changes. Motion can be disabled.
- **123 synth models** with sixteen variation positions, including SID Chip, DCO Poly and FM SysEx bank/voice support. Selected FM voice data is stored with the song.
- **Sampling and recording**: archive discovery, range selection, sample editing, slices, CHOP review, granular exploration and internal resampling.
- **Drumlab and Harmony**: editable generated rhythm material and 48 progression templates with locks and phrase writing.
- **Performance tools**: eight Punch banks, track mute/solo, effects, master processing and mix/stem rendering.
- **Editing and saving hardening** across undo, previews, memory budgets and WAV publication.
- **A 130-page illustrated English handbook**, a 7:15 walkthrough with captions and chapter marks, and new sample-based soundtrack mixes.

## Install or update

Choose the exact operating-system/profile package. For a fresh card use First Install; for an existing installation use Program Update. Back up your data and follow START-HERE.

The Android APK uses the existing com.kidbunka.trackwerk ID and release signer. VersionCode 101004 updates both the 1.0 APK (100000) and the 1.1 development APK (101002). Install over the app; do not uninstall to update.

The Miyoo updater recognises 25 verified predecessors, including the b3aecfa development programs. The included menu-name helper corrects legacy Apps/Ports metadata to TrackWerk 1.1; the optional Plus offline installation is labelled TrackWerk 1.1 Offline.

The factory collection is unchanged: the exact 1.0 Factory Presets archive is supplied alongside the 1.1 packages. Five public demo projects use internal synth engines. Raw commercial soundtrack samples are excluded.

## Verification and limits

All 83 host checks pass on the release application source. The Linux binaries were started in isolated ARM guests; the RGB20S guest verifies loader/startup and its fixture probes, not a physical DRM frame. Android 1.0 -> 1.1 was installed and started in an isolated emulator with generated private files retained. The newer APK's ID, signer, ABIs and 16 KiB alignment are verified. No physical card or device was modified by this release preparation; the previous b3aecfa application had already been confirmed running on the four handhelds by Kid Bunka.

Network providers, MIDI and external audio input depend on the profile and hardware. Mini offline is local. YouTube starts disabled and requires explicit consent. No universal polyphony or CPU claim is made. Check a recording's actual reuse terms before releasing sampled music.

TrackWerk by Kid Bunka, based on PocketTracker by conanizer and contributors. GPL-3.0-or-later. Complete corresponding application source, component notices and exact Miyoo SDL source are release assets.
