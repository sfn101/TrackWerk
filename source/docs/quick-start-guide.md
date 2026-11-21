# TrackWerk 1.3 — Your first beat

This is a short route to one audible pattern. The [illustrated handbook](https://kidbee85-beep.github.io/TrackWerk/guide.html) covers the wider Studio, sampling and performance workflow. The [core reference](manual-en.md) explains the detailed tracker commands.

## Or let TrackWerk guide you (1.2)

From TrackWerk 1.2 the app can walk you through this in ten small steps: press **SELECT** for help, then **START** to begin **First beat**. It starts a new project with its own name (FIRSTBEAT, FIRSTBEAT2 …), so no saved song is replaced; if your current work is unsaved, it asks first. A card over the scope shows the step and the buttons; each step is ticked off by what you actually do. In help, **L** goes back one step and **START** ends the tutorial. Start it from the tracker help: on the Studio, SELECT opens the Studio's own help, where START does not begin it, so press **B** to leave the Studio first, then **SELECT** and **START**. The sound comes from a built-in synth, so no sample file or internet connection is needed.

## Before pressing anything

A tracker reads notes **downward**. A phrase contains 16 steps, labelled `00` to `0F`. These are hexadecimal row numbers: `0A` means step 10 and `0F` means step 15.

- **Instrument**: the sound a note uses.
- **Phrase**: a short pattern of notes.
- **Chain**: a list of phrases to play in order.
- **Song**: the arrangement of chains on eight tracks.

Numbers in the Song and Chain screens are **references**, not audio files. Two cells with the same phrase number share the same phrase. Editing it changes what both cells play.

## The few controls you need

| Input | What to expect |
|---|---|
| D-pad | Move the cursor without changing the value. |
| A | Insert a value, confirm, or open the selected control. |
| A + D-pad | Change the selected value. Follow its on-screen help for the step sizes. |
| A + B | Clear the selected value. |
| R + Left / Right | Move between Song, Chain, Phrase and Instrument. |
| START | Play or stop the current song, chain or phrase. In the sample browser it previews the selected file. |
| SELECT | Open the current help when enabled. |

On Android these names mean the app's logical buttons, including the virtual buttons when shown. Use your configured mapping on a physical controller. A click in the grid and a held A+direction are different actions.

## 1. Give Android access to your sounds

Open **INSTRUMENT → LOAD** or **PROJECT → LOAD**. At the list of granted folders, select **ADD FOLDER…**, press A, then choose a writable folder in Android's folder picker. A library outside that folder needs its own grant. Granting one folder does not scan the whole phone.

For this lesson, put a short kick or percussion WAV that you may use in an accessible folder. No online service or paid sample library is required. Use the matching installation package for your device; Linux handheld builds already have their own configured data folder.

Keep an existing project saved before starting a new one. Do this lesson in a new project or a separately saved copy, rather than overwriting a song you want to keep.

## 2. Connect Song to Chain to Phrase

1. Go to **SONG**, choose an empty cell on the first track and tap A. A chain number should replace `--`.
2. Hold R and press Right to open that chain. In its first **PH** cell, tap A to insert a phrase reference.
3. Hold R and press Right again. You are now editing that phrase.

**Checkpoint:** the header says PHRASE and the rows run `00` to `0F`. The cursor is your edit position; the moving play marker appears only during playback.

## 3. Load one sound

In the phrase's **INST** column, note the instrument number. Open that instrument with R+Right, then choose **LOAD → Samples / Audio**. Enter folders with A. Highlight your kick and press START to preview it; press A to load it.

B closes the browser. For the parent folder use **R+Left**, or select `..` and press A. Returning from a browser is different from moving between tracker screens.

**Checkpoint:** the Instrument screen names your loaded sound. A note using a different instrument number will play a different sound, or remain silent if that slot is empty.

## 4. Put four hits in the phrase

Return to PHRASE with R+Left. Move to row `00`, column **NOTE**, and tap A to insert a note. Set the **INST** value on that row to the instrument you just loaded. Repeat on rows `04`, `08` and `0C` with the same pitch and instrument.

Press START. You should hear four evenly spaced hits as the phrase repeats. Press START again to stop. Leave the FX columns alone for this first checkpoint.

If you hear nothing, check the instrument number, whether a file was actually loaded, the instrument/track/master levels and whether the track is muted. Increasing Drive is not the first troubleshooting step.

## 5. Change something and hear the result

While the phrase plays, move to a note and change its pitch with A+Left/Right. Change one hit's **VOL** value to make it softer. Clear one note with A+B and put it back with A.

You have now edited pitch, dynamics and rhythm. Stop playback before moving on. A+B on an already empty note can insert OFF, so check what the cell shows rather than repeatedly clearing it blindly.

## 6. Hear it from the Song screen

Return through CHAIN to SONG using R+Left. Select the song row you populated and press START. The chain should call the phrase you just made.

If phrase preview works but the song is silent, check the references: Song must point to the intended chain, and that chain's PH cell must point to the intended phrase. Do not solve a reference problem by loading the sample repeatedly.

For an independent second pattern, allocate or clone a separate phrase. Reusing the same phrase number intentionally reuses its notes. The handbook explains allocation, cloning and multi-track arrangements.

## 7. Save, close and open your work

Stop playback, open PROJECT and save under a new project name. Confirm the displayed destination. Load that saved project again and check both the notes and the sound.

A `.ptp` contains the arrangement and references to media; it is not a guarantee that every referenced sample is embedded. Keep the project's media available at the saved paths. Use the documented collection/export tools when making a portable copy; Android content-URI references do not automatically become Linux paths.

## Where to go next

- **Another sound:** INSTRUMENT → LOAD also offers synth/factory and instrument-preset routes. Open a model/preset, audition it and check the target instrument before applying.
- **Slicing:** load a sample, open its Sample Editor and use the page help for zoom, markers, CHOP and sending a selection to an instrument.
- **Sources:** STUDIO → SOURCES can find recordings or radio on supported online profiles. Listen, choose IN/OUT and save an excerpt into the editor. Source rights vary.
- **Harmony and Drumlab:** generate a starting point, audition it, then explicitly apply it to the project. They complement the tracker rather than replace its references.
- **Punch and resampling:** use the current on-screen help to choose a bank, hold an effect and record the app's internal mix. External input recording is a separate path.

## MIDI to an external synthesizer

MIDI output is already part of TrackWerk. Open **PROJECT → MIDI**, choose OUTPUT, then use TEST. TEST sends a short C-4 on the channel shown beside it; **A+Left/Right** picks 01–16, so set it to the channel your receiver listens on. The line under the list says what the receiver needs, and the status says what happened: sent, no MIDI device, OUTPUT off, device not connected, or send failed. Set an instrument's TYPE to EXTERNAL and its CHAN to the receiver's channel to sequence it. An EXTERNAL instrument produces no sound inside TrackWerk itself.

Check the status line. Use PANIC if an external note sticks. USB host support, adapters and the receiving firmware still need a real device test; a listed port is not proof of an audible FM-1 connection.

## Updates and recording

Use the package for the operating system already on your device. Existing Android installations need a compatible signed update; do not uninstall to bypass an identity/signature mismatch. On the handhelds, keep the established projects, samples, presets and settings and use the matching program updater.

The normal Miyoo Mini is offline. Online sources, microphone and class-compliant interfaces depend on the selected profile and actual connected hardware. Direct iPhone-to-Brick USB-C capture failed in the user's tested setup; the presence of an experimental helper does not make that path verified.

For input recording, use **INSTRUMENT → RECORD** or the Sample Editor's RECORD control, choose a detected input and record a short test. Waiting for input and receiving real frames are different states. Use the take only after confirming that it contains the intended sound.
