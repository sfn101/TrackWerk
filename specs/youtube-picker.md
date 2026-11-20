# YouTube as a sample picker (Studio > Sources)

Agreed 2026-11-19 with the user, without a grill-me session (they asked to treat it as a fix).

## Goal

YouTube in SOURCE is for getting a specific track to sample. Before this change it was built as a browser: typed searches
had the genre added, results included hour-long compilations, playlists and channels with no length shown, and there was
no way to give it a link. It now works as a picker.

## Status

- **Windows:** tested by hand on 2026-11-20, covering link paste and lengths. A typed search after the START fix (4863b4b) has not been tried by hand yet.
- **Android:** UNTESTED on a device. It only builds in CI.
- **Linux desktop:** CI checks only.
- If Android YouTube misbehaves, start from this spec and the commits f3e64b1 and 4863b4b. Listed under "Needs device testing" in ROADMAP.md.

## Platforms

Android, Windows and Linux desktops. Not on handhelds: builds with `PT_HANDHELD` (Brick, RGB20S) never look for yt-dlp, so
the provider shows as unavailable. The Miyoo builds had no YouTube already.

## Controls

- **Provider = YouTube:** A A turns it on, A turns it off (unchanged).
- **Search row, three columns:**
  - **Search:** the typed words only. A typed or pasted YouTube link opens that video.
  - **Random:** the genre plus one of: instrumental, drum break, rare groove, loop, vintage.
  - **Paste link:** shows the link field.
- **Link field:** it reads the system clipboard and shows the first YouTube link in it.
  - A pastes again when nothing usable is shown, and otherwise opens the link.
  - B closes the field.
  - While the field is open, no other button and no chord does anything (`app.cpp` sends every button to Studio, and `studio_button` sends them to the field first).
- **Android share:** "Share > TrackWerk" in the YouTube app or a browser.
  - The link waits in the state as `shared_link` / `shared_link_id`.
  - The next time SOURCE polls, the field opens on YouTube with that link, once per share.
- **Results list:** each result shows its length (m:ss).

## Filtering

- Results are videos only. Channels, playlists, and live or upcoming streams are left out.
- The length must be between 1 second and 15:00 (`YOUTUBE_MAX_SECONDS`, and `YouTubeFacade.MAX_SECONDS` on Android).
- A pasted link to a stream or a longer video is refused, and the message says why and gives the length.
- Accepted link forms: `youtube.com/watch?v=`, `youtu.be/`, `/shorts/`, `/live/`, `/embed/`, with or without `www.`, `m.` or `music.`. Only the 11-character ID is used, and the URL is rebuilt from it.

## Platform effects

- **Desktop:** `shell/linux/source-sampler.cpp` (yt-dlp).
  - `ytsearch30`, so enough results remain after the filter.
  - The details of a link are read with `--ignore-no-formats-error`, so a stream can still be described.
- **Clipboard:** `SDL_GetClipboardText`, through `SourceSamplerState::readClipboard`, set in `app.cpp`. This covers Android too, since it runs on SDLActivity.
- **Android:** `YouTubeFacade` (NewPipe Extractor; no yt-dlp on Android), `SourceSampler.kt`, a `SEND text/plain` intent filter on `MainActivity`, which is singleTask.

## Out of scope

- A web view inside the app.
- Picking a section of a video before downloading it.
- Removing yt-dlp from the handheld packages (`package_brick.py`, `r36s/package.py`, launcher checks). The handheld app no longer uses it, but the release tooling still ships it.

## Help

- The SELECT help in `studio_help.cpp`: "Sources / YouTube" and "Sources / YouTube link".
- The hint line for the Search row on YouTube.
- The YouTube section in `docs/manual-en.md` (File Management).
