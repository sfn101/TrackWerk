#pragma once

// SETTINGS. Which rows exist comes from PlatformCaps, where they sit from settings_row_layout.h.
// ⚠️ The module edits indices and flags; what an index means (which layout, which .png) is the
// platform's, handed in as text — an empty text hides the row.
// ⚠️ Plain A only acts (THEME opens the editor, TEMPLATE saves/clears); values change with A+DPAD.

#include <string>
#include <vector>

#include "songcore/midi_map.h"   // MIDI_CTL_CH_ALL — what the CTL CH row starts on
#include "ui/canvas.h"
#include "ui/cursor.h"
#include "ui/platform_caps.h"
#include "ui/settings_row_layout.h"
#include "ui/theme.h"

namespace pt::ui {

// SETTINGS > LATENCY: "10.7 MS", or ">= 10.7 MS" when the backend knows only its buffer (the real
// delay is at least that); "-" before the output has opened. Integer math: the same text everywhere.
inline std::string latency_text(int frames, int sampleRate, bool measured) {
    if (frames <= 0 || sampleRate <= 0) return "-";
    const long long tenths = (static_cast<long long>(frames) * 10000 + sampleRate / 2) / sampleRate;
    return std::string(measured ? "" : ">= ") + std::to_string(tenths / 10) + "." +
           std::to_string(tenths % 10) + " MS";
}

// SETTINGS > HELP. ⚠️ The value is what settings.json stores — append, never reorder.
enum class HelpMode { OFF = 0, SHORT = 1, FULL = 2 };

// Everything SETTINGS edits; the shell round-trips it through settings.json. The counts sit beside
// the indices because only the platform knows a cycle's range.
// SETTINGS > TOUCH BUTTONS. ⚠️ settings.json stores the number.
inline constexpr int TOUCH_AUTO = 0, TOUCH_ALWAYS = 1, TOUCH_OFF = 2;

// SETTINGS > SCALING, crispest to softest. ⚠️ settings.json stores the number.
inline constexpr int SCALING_INT = 0, SCALING_SHARP = 1, SCALING_BILINEAR = 2;

struct SettingsValues {
    // ── The device rows (Android; the shell hides them) ──────────────────────────────────────────
    int  layoutIndex       = 0;
    int  layoutCount       = 1;
    int  skinIndex         = 0;
    int  skinCount         = 0;   // 0 = this layout is not skinned → no second column on LAYOUT

    // ⚠️ What persists is the stable id string (`portrait_skin`), not `skinIndex`; the shell
    // resolves it at boot (device_skin.h).
    std::string portraitSkin = "amiga-2";

    int  overlayIndex      = 0;   // 0 = "OFF"; 1.. = a file
    int  overlayCount      = 1;   // "OFF" + however many files
    int  overlayStrength   = 128;

    // ⚠️ Persisted as the id string too (`overlay_name`); "OFF" is none.
    std::string overlayName = "OFF";

    // ⚠️ First launch only — `load_settings` overwrites these. Button feedback is on (LO) so a
    // first press answers.
    bool buttonSoundEnabled = true;
    int  buttonSoundVolume  = 0x0F;
    bool buttonVibroEnabled = true;
    int  vibroPower         = 64;
    bool autosaveResumeAuto = false;
    // The First beat tutorial was finished once (ui/first_beat.h). Written only when true.
    bool firstBeatDone = false;

    // How the pad's face buttons are printed: 0 AUTO, 1 XBOX, 2 NINTENDO (ui::AbxyLayout).
    int  abxyIndex          = 0;

    // TOUCH BUTTONS (specs/wide-mode.md): TOUCH_AUTO shows the on-screen buttons when no controller
    // is connected, TOUCH_ALWAYS even with one (the old touchButtonsWithPad), TOUCH_OFF never (a
    // two-finger tap shows and hides them). Its own value: the LAYOUT list changes length with a pad.
    int touchButtons = TOUCH_AUTO;

    // ── METRONOME (every platform): a click per quarter note while playing, never exported ──────
    bool metronomeEnabled = false;
    int  metronomeVolume  = 0x80;

    // ── HELP — what SELECT shows: 0 OFF, 1 SHORT (panel), 2 FULL (overlay) ───────────────────────
    // ⚠️ This default reaches upgrading installs, so it is the long-standing behaviour.
    int  helpMode         = 1;

    // ── Sample interpolation (specs/sample-interpolation.md), sample-interp.h's numbers ──────────
    // INTERP DEFAULT: what a new song's PROJECT > INTERP starts on. INTERP LIMIT: the highest mode live
    // playback uses; renders ignore it. The handheld shell lowers the factory limit before loading.
    int  interpDefault    = 2;   // CUBIC
    int  interpLimit      = 4;   // SINC 8 LP: every mode

    // ── KEY BAR: the screen's own shortcuts at the canvas's foot (ui/key_bar.h) ────────────────
    // ⚠️ Off here so the tools and checks draw the screens as recorded; the app turns it on before
    // reading settings.json (shell/app.cpp), so a user gets it on until they switch it off.
    bool keyBar           = false;

    // ── The rows every platform has ──────────────────────────────────────────────────────────────
    // SCALING (specs/sharp-scaling.md): SCALING_INT, SCALING_SHARP or SCALING_BILINEAR. A new install
    // gets SHARP; an older settings.json's scalingBilinear carries over (settings_store.cpp).
    int scaling             = SCALING_SHARP;
    // SCREEN: 16:9 (an 854-wide canvas) instead of 4:3. Read at startup only; a change waits for a
    // restart. Saved only when on, so an existing settings.json is unchanged.
    bool wideScreen         = false;
    // Stable appearance-only ID; 0 preserves existing installs. Independent of theme presets.
    int fontStyle           = 0;
    // Appearance > Motion: the Scene animations (ui/scene_animation.h). OFF keeps the screens still;
    // the playing-row band and the scope stay, they carry information. Saved only when OFF, so an
    // existing settings.json is unchanged byte for byte.
    bool sceneMotion        = true;
    // Sources > Provider: YouTube search through yt-dlp. Off until the player opts in (it runs a
    // third-party helper and conflicts with YouTube's terms). Saved only when ON, like sceneMotion.
    bool sourceYoutube      = false;
    bool insertBefore       = true;
    bool cursorRemember     = false;
    bool notePreviewEnabled = true;

    // FOLDER = REMEMBER / REFRESH: the sample browser's location, independent of other sources.
    bool        rememberFolder   = true;
    std::string lastSampleFolder;
    std::string lastSampleSelection;
    int         lastSampleCursor = 0;
    int         lastSampleScroll = 0;
    int         lastSampleSort = 2;  // FileSortMode::NAME_ASC

    // Ordered factory preset IDs for the Granular Favorites bank, independent of the current song.
    std::vector<int> sampleGranularFavorites;

    // The sound browser (Studio PRESET picker, 1.2): starred and recently used factory sounds, keyed
    // by their path below Presets/Instruments/PocketStudio_MegaPack, and the level the browser
    // previews at (percent; the preview only, never an instrument, track or master).
    std::vector<std::string> soundFavorites;
    std::vector<std::string> soundRecent;
    int soundPreviewLevel = 100;

    // NAV = POOL / SONG. POOL: B+LEFT/RIGHT scroll the chain and phrase pools; SONG: B+D-pad walks
    // the arrangement. ⚠️ This default reaches existing installs.
    bool navSongRelative = true;

    // ── MIDI — the MIDI screen's, kept here for settings.json; this machine's cable, not the song's
    // ⚠️ The device is the name string, never an index (port lists reorder). "OFF" = none, "AUTO" =
    // the first plugged in.
    std::string midiOutDevice = "AUTO";

    // Opened at boot by `InputDispatcher::boot_midi_in_port`.
    std::string midiInDevice = "AUTO";

    // Signed ms; positive = MIDI leaves later than the audio.
    // ⚠️ Read through `midi_offset_in_force()`: under AUTO this is only the last dialled value.
    int         midiOffsetMs  = 0;

    // AUTO: from the latency the audio device reports — one buffer, so close rather than exact.
    bool        midiOffsetAuto = true;

    // SYNC OUT: 24 PPQN clock, transport and song position. OFF by default: a synth set to external
    // sync sits silent without clock.
    bool        midiSyncOut   = false;

    // SYNC IN: `songcore::MidiSyncInMode` — OFF, TEMPO, FULL. Read by the frame loop every tick.
    int         midiSyncIn    = 0;

    // CTL CH: the channel a mapping knob must arrive on (midi_map.h). −1 = OFF, else 0-15.
    // ⚠️ ALL is safe only because a CC is claimed, not reserved: one driving no mapping routes as
    // before.
    int         midiControlChannel = songcore::MIDI_CTL_CH_ALL;
    // MIDI TEST's channel, 0-15 (shown 01-16): the receiver must listen there. Written when not 1.
    int         midiTestChannel = 0;
    // A live key: 1 = MONO on the SONG cursor's track, 2..8 = POLY over that many tracks from it.
    int         midiInVoices = 4;
    // OFF plays every live key at full strength.
    bool        midiVelocity = true;
    // AUDIO OUT: the name persists; index and count are the cycle over the shell's list (0 = SYSTEM).
    std::string audioOutput   = "SYSTEM";
    int         audioOutIndex = 0;
    int         audioOutCount = 1;

    // ⚠️ VISUALIZER lives on the theme (`Theme::visualizerType`), carried across a theme change.

    // ── Debug ────────────────────────────────────────────────────────────────────────────────────
    bool traceEnabled = false;
};

struct SettingsState {
    const SettingsValues& values;

    int cursorRow    = 0;   // a SettingsRow, not a position on screen
    int cursorColumn = 1;

    // What the current index names on this platform. Braced defaults keep aggregate init warning-free.
    std::string layoutText{};
    std::string skinText{};
    std::string overlayText = "OFF";
    std::string audioOutText = "SYSTEM";
    std::string latencyText  = "-";
    std::string appVersion{};
    bool        scalingFallback = false;   // SHARP is drawing with BILINEAR: no render targets here
    std::string themeName   = "CLASSIC";

    PlatformCaps caps{};
    Theme        theme = theme_classic();
};

struct SettingsInputResult {
    bool modified = false;
};

class SettingsModule {
public:
    static constexpr int WIDTH  = 510;
    static constexpr int HEIGHT = 392;

    // In VisualizerType order.
    static const std::vector<std::string>& visualizer_names();

    void draw(Canvas& c, int x, int y, const SettingsState& s) const;

    CursorContext cursor_context(const SettingsState& s) const;

    // Writes into `values` (and `theme`, for VISUALIZER).
    SettingsInputResult handle_input(SettingsValues& values, Theme& theme, const PlatformCaps& caps,
                                     int cursor_row, int cursor_column,
                                     const InputAction& action) const;
};

}  // namespace pt::ui
