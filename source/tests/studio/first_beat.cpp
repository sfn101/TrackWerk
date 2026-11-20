// ptfirst-beat-check: the First beat tutorial (ui/first_beat.h) played through with real buttons, the
// way a newcomer would: help → START, R + arrows to the screens, a built-in synth applied from the
// INSTRUMENT screen, four notes, START, a changed and a deleted note, the phrase into a chain and the
// song, SAVE and LOAD on PROJECT. Every step must be ticked off by the action alone. Also: unsaved
// work asks first (NO keeps it), an existing FIRSTBEAT.ptp is never overwritten, L steps back, START
// in help ends it, and firstBeatDone survives a settings save → load while an older file stays as it was.
// usage: ptfirst-beat-check <new run directory>
#include "audio-engine.h"
#include "songcore/host.h"
#include "songcore/project_io.h"
#include "ui/button_mapper.h"
#include "ui/first_beat.h"
#include "ui/input_dispatcher.h"
#include "ui/layout.h"
#include "ui/settings_row_layout.h"
#include "ui/settings_store.h"
#include "ui/std_filesystem.h"
#include "ui/studio.h"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <iostream>
#include <memory>

using namespace pt::ui;

namespace {

// Every FIRSTBEAT name is taken (as after 9'999 tutorials), without writing those files.
struct FullFs : StdFileSystem {
    using StdFileSystem::StdFileSystem;
    bool file_exists(const std::string& path) override {
        return path.find("FIRSTBEAT") != std::string::npos || StdFileSystem::file_exists(path);
    }
};

template <class Fs = StdFileSystem>
struct Rig {
    std::unique_ptr<AudioEngine> engine = std::make_unique<AudioEngine>();
    songcore::SongcoreHost host{engine.get(), 48000};
    AppState state;
    Fs fs;
    InputDispatcher dispatch;
    MapperState mapper;
    uint64_t now = 1000;
    ButtonMods held;
    explicit Rig(const std::filesystem::path& dir) : fs((dir / "data").string()), dispatch(state, host, fs) {
        host.new_project();
        state.project = &host.edit_project();
        state.currentScreen = ScreenType::SONG;
    }
    void set(Button b, bool down) {
        switch (b) {
            case Button::A: held.a = down; break;
            case Button::B: held.b = down; break;
            case Button::L_SHIFT: held.l = down; break;
            case Button::R_SHIFT: held.r = down; break;
            case Button::SELECT: held.select = down; break;
            default: break;
        }
    }
    void send(Button b, ButtonAction a) {
        now += 40;
        dispatch.set_now(static_cast<long long>(now));
        const ButtonEvent e{b, a, held};
        if (!studio_button(e, state, host, *engine, fs)) handle_button(e, dispatch, mapper, now);
        state.isPlaying = host.is_playing();   // the shell's per-frame readback
        first_beat_poll(state, state.isPlaying);
    }
    void press(Button b) { set(b, true); send(b, ButtonAction::PRESSED); }
    void release(Button b) { send(b, ButtonAction::RELEASED); set(b, false); }
    // A human pause before each A: two A presses within 300 ms are the A,A double-tap.
    void tap(Button b) { if (b == Button::A) now += 400; press(b); release(b); }
    void hold_tap(Button mod, Button b) { press(mod); tap(b); release(mod); }
    void help_start() { tap(Button::SELECT); assert(state.helpOpen || state.helpFull); tap(Button::START); }
    FirstBeatStep step() const { return state.firstBeat.step; }
};

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2 || std::filesystem::exists(argv[1])) {
        std::cerr << "usage: ptfirst-beat-check <new run directory>\n";
        return 2;
    }
    const std::filesystem::path dir = argv[1];
    std::filesystem::create_directories(dir);
    Rig<> r(dir / "a");

    // ── The invitation beside SELECT HELP: on an empty song, drawn; gone with a chain, when done,
    //    or with help OFF ─────────────────────────────────────────────────────────────────────────
    {
        assert(first_beat_invite(r.state));
        const auto draw = [&](AppState& st) { TrackerLayout l; Canvas c; l.draw(c, st); return c; };
        const Canvas shown = draw(r.state);
        r.state.settings.firstBeatDone = true;
        assert(!first_beat_invite(r.state));
        const Canvas done = draw(r.state);
        int differ = 0;
        for (int i = 0; i < DESIGN_W * 80; ++i) differ += shown.pixels()[i] != done.pixels()[i];
        assert(differ > 200);   // the line in the scope strip
        r.state.settings.firstBeatDone = false;
        r.state.settings.helpMode = static_cast<int>(HelpMode::OFF);
        const Canvas off = draw(r.state);
        int offDiff = 0;
        for (int i = 0; i < DESIGN_W * 80; ++i) offDiff += off.pixels()[i] != shown.pixels()[i];
        assert(offDiff > 200);   // no SELECT HELP, no invitation
        r.state.settings.helpMode = static_cast<int>(HelpMode::SHORT);
    }

    // ── Unsaved work: help → START asks first; NO keeps the work and starts nothing ───────────────
    r.state.projectVersion = 3;   // as after an edit
    r.help_start();
    assert(r.state.confirm.is_open() && r.state.confirm.kind == ConfirmDialogState::Kind::FIRST_BEAT);
    r.tap(Button::B);
    assert(!r.state.confirm.is_open() && !r.state.firstBeat.active && r.state.project_dirty());
    // YES starts it in a new project.
    r.help_start();
    r.tap(Button::A);
    assert(r.state.firstBeat.active && r.step() == FirstBeatStep::SONG_CHAIN);
    assert(r.host.project().name == "FIRSTBEAT" && !r.state.project_dirty());

    // 1 · SONG: A on the first row of track 1 puts a chain in.
    assert(r.state.currentScreen == ScreenType::SONG);
    while (r.state.cursorColumn < 1) r.tap(Button::DPAD_RIGHT);
    r.tap(Button::A);
    assert(r.step() == FirstBeatStep::CHAIN_PHRASE);
    assert(!first_beat_invite(r.state));   // running, and the song has a chain
    // 2 · R + Right into that chain; A puts a phrase in.
    r.hold_tap(Button::R_SHIFT, Button::DPAD_RIGHT);
    assert(r.state.currentScreen == ScreenType::CHAIN);
    while (r.state.cursorColumn < 1) r.tap(Button::DPAD_RIGHT);
    r.tap(Button::A);
    assert(r.step() == FirstBeatStep::GO_PHRASE);

    // 3 · R + Right into the phrase.
    r.hold_tap(Button::R_SHIFT, Button::DPAD_RIGHT);
    assert(r.state.currentScreen == ScreenType::PHRASE && r.step() == FirstBeatStep::SOUND);

    // 2 · INSTRUMENT: down to STUDIO >, A; in the synth page down to APPLY, A; B back.
    r.hold_tap(Button::R_SHIFT, Button::DPAD_RIGHT);
    assert(r.state.currentScreen == ScreenType::INSTRUMENT);
    for (int i = 0; i < 4; ++i) r.tap(Button::DPAD_DOWN);
    r.tap(Button::A);
    assert(r.state.studioOpen && r.state.studioTab == 0);
    for (int i = 0; i < 12 && r.step() == FirstBeatStep::SOUND; ++i) {
        r.tap(Button::DPAD_DOWN);
        if (r.state.studioRow == studio_synth_apply_row(r.state)) r.tap(Button::A);
    }
    assert(r.step() == FirstBeatStep::NOTES);
    assert(!songcore::instrument_is_free(r.host.project().instruments[0]));
    while (r.state.studioOpen) r.tap(Button::B);

    // 3 · back to PHRASE, four notes in the NOTE column.
    r.hold_tap(Button::R_SHIFT, Button::DPAD_LEFT);
    assert(r.state.currentScreen == ScreenType::PHRASE);
    while (r.state.cursorColumn < 1) r.tap(Button::DPAD_RIGHT);
    for (int n = 0; n < 4; ++n) {
        r.tap(Button::A);
        r.tap(Button::DPAD_DOWN);
    }
    assert(first_beat_note_count(r.state) >= 4);
    assert(r.step() == FirstBeatStep::PLAY);

    // 4 · START plays; START again stops.
    r.tap(Button::START);
    assert(r.step() == FirstBeatStep::CHANGE);
    r.tap(Button::START);
    assert(!r.host.is_playing());

    // Review 59: another phrase that already has four notes is not "write four notes" (checked
    // here by stepping back to NOTES and looking at the other phrase).
    {
        const int here = r.state.currentPhrase;
        auto& full = r.host.edit_project().phrases[static_cast<size_t>(here + 2)];
        for (int i = 0; i < 4; ++i) full.steps[static_cast<size_t>(i)].note = songcore::Note::C4();
        FirstBeat saved = r.state.firstBeat;
        r.state.firstBeat.step = FirstBeatStep::NOTES;
        r.state.firstBeat.phraseAtStep = here;
        r.state.currentPhrase = here + 2;
        assert(!first_beat_step_done(r.state, false));
        for (int i = 0; i < 4; ++i) full.steps[static_cast<size_t>(i)].note = songcore::Note::EMPTY();
        r.state.currentPhrase = here;
        r.state.firstBeat = saved;
    }

    // Review 59: moving to another phrase with other notes is no changed note.
    {
        const int here = r.state.currentPhrase;
        auto& other = r.host.edit_project().phrases[static_cast<size_t>(here + 1)];
        other.steps[0].note = songcore::Note{5, 3};
        other.steps[1].note = songcore::Note{7, 3};
        r.state.currentPhrase = here + 1;
        first_beat_poll(r.state, false);
        assert(r.step() == FirstBeatStep::CHANGE);
        other.steps[0].note = songcore::Note::EMPTY();
        other.steps[1].note = songcore::Note::EMPTY();
        r.state.currentPhrase = here;
    }

    // 5 · hold A on a note, Right: a different note.
    r.tap(Button::DPAD_UP);   // onto the fourth note
    r.press(Button::A);
    r.tap(Button::DPAD_RIGHT);
    r.release(Button::A);
    assert(r.step() == FirstBeatStep::REMOVE);

    // 6 · on the note, hold A and press B: the note is cleared (A+B, as help says).
    r.now += 400;
    r.press(Button::A);
    r.tap(Button::B);
    r.release(Button::A);
    assert(r.step() == FirstBeatStep::SAVE);

    // 9 · R + Left twice to SONG, R + Up to PROJECT, the PROJECT row, SAVE.
    r.hold_tap(Button::R_SHIFT, Button::DPAD_LEFT);
    r.hold_tap(Button::R_SHIFT, Button::DPAD_LEFT);
    assert(r.state.currentScreen == ScreenType::SONG);
    r.hold_tap(Button::R_SHIFT, Button::DPAD_UP);
    assert(r.state.currentScreen == ScreenType::PROJECT);
    for (int i = 0; i < 12 && r.state.projectCursorRow != static_cast<int>(ProjectRow::PROJECT); ++i)
        r.tap(Button::DPAD_DOWN);
    assert(r.state.projectCursorRow == static_cast<int>(ProjectRow::PROJECT));
    while (r.state.projectCursorColumn < 1) r.tap(Button::DPAD_RIGHT);
    r.tap(Button::A);   // SAVE
    assert(r.step() == FirstBeatStep::RELOAD);
    const std::string saved = r.fs.projects_directory() + "/FIRSTBEAT.ptp";
    assert(std::filesystem::exists(saved));

    // 10 · LOAD, then A on FIRSTBEAT.ptp in the browser.
    r.tap(Button::DPAD_RIGHT);   // LOAD
    r.tap(Button::A);
    assert(r.state.currentScreen == ScreenType::FILE_BROWSER);
    for (int i = 0; i < 40; ++i) {
        const auto* item = r.state.fileBrowser.current();
        if (item && item->displayName.find("FIRSTBEAT") != std::string::npos) break;
        r.tap(Button::DPAD_DOWN);
    }
    r.tap(Button::A);
    assert(r.step() == FirstBeatStep::DONE && r.state.settings.firstBeatDone);

    // Help → START closes the finished card.
    r.help_start();
    assert(!r.state.firstBeat.active);

    // ── A second run: FIRSTBEAT.ptp exists, so the tutorial project is FIRSTBEAT2; L steps back ──
    const std::string before = [&] { std::string b; r.fs.read_file(saved, b); return b; }();
    r.help_start();
    assert(r.state.firstBeat.active && r.host.project().name == "FIRSTBEAT2");
    while (r.state.cursorColumn < 1) r.tap(Button::DPAD_RIGHT);
    r.tap(Button::A);
    assert(r.step() == FirstBeatStep::CHAIN_PHRASE);
    r.tap(Button::SELECT);
    r.tap(Button::L_SHIFT);   // step back
    assert(r.step() == FirstBeatStep::SONG_CHAIN && !r.state.helpOpen && !r.state.helpFull);
    for (int i = 0; i < 5; ++i) first_beat_poll(r.state, false);   // the card stays, frame after frame…
    r.tap(Button::DPAD_DOWN);                                        // …and over input that changes nothing
    r.tap(Button::DPAD_UP);
    assert(r.step() == FirstBeatStep::SONG_CHAIN);
    assert(std::string(first_beat_text(r.step()).title).find("1/10") == 0);
    r.hold_tap(Button::R_SHIFT, Button::DPAD_RIGHT);   // …and moves on once the player acts again
    assert(r.state.currentScreen == ScreenType::CHAIN && r.step() == FirstBeatStep::CHAIN_PHRASE);
    r.help_start();   // START in help ends it
    assert(!r.state.firstBeat.active);
    std::string after;
    r.fs.read_file(saved, after);
    assert(after == before);   // the first tutorial's file is untouched

    // ── Started from other screens: it always begins on SONG, help and Studio put away ─────────
    for (ScreenType from : {ScreenType::PROJECT, ScreenType::INSTRUMENT}) {
        r.state.currentScreen = from;
        r.state.projectVersion = r.state.savedProjectVersion;   // nothing unsaved
        r.help_start();
        assert(r.state.firstBeat.active && r.state.currentScreen == ScreenType::SONG && !r.state.studioOpen);
        assert(!r.state.helpOpen && !r.state.helpFull && r.step() == FirstBeatStep::SONG_CHAIN);
        r.help_start();   // end it again
        assert(!r.state.firstBeat.active);
    }

    // ── Review 59: an unsaved sample edit (RAM only) refuses the start, nothing changes ─────────
    {
        const std::string blob = songcore::serialize_project(r.host.project());
        const ScreenType was = r.state.currentScreen;
        r.state.currentScreen = ScreenType::SAMPLE_EDITOR;   // help opened over the editor
        r.state.sampleEditor.isModified = true;
        r.help_start();
        assert(!r.state.firstBeat.active && !r.state.confirm.is_open());
        assert(r.state.statusMessage.find("SAMPLE EDIT") != std::string::npos);
        assert(songcore::serialize_project(r.host.project()) == blob);
        // Review 60: the full-screen editor shows no status line, so the reason is in its own line,
        // and it is really drawn: the rendered editor differs from the same editor without it.
        assert(r.state.sampleEditor.toolMessage == FIRST_BEAT_SAMPLE_HINT);
        assert(r.state.sampleEditor.isModified);   // nothing saved or discarded for the player
        {
            TrackerLayout shown, plain;
            Canvas withHint, without;
            shown.draw(withHint, r.state);
            const std::string kept = r.state.sampleEditor.toolMessage;
            r.state.sampleEditor.toolMessage.clear();
            plain.draw(without, r.state);
            r.state.sampleEditor.toolMessage = kept;
            int differing = 0;
            for (int i = 0; i < DESIGN_W * DESIGN_H; ++i) differing += withHint.pixels()[i] != without.pixels()[i];
            assert(differing > 100);
        }
        r.state.sampleEditor.toolMessage.clear();
        // A closed editor's leftover flag is no guard: leaving with unsaved work already asked.
        r.state.currentScreen = was;
        assert(first_beat_blocked(r.state) == nullptr);
        r.state.sampleEditor.isModified = false;
    }

    // ── Review 59: PROJECT > NEW during the tutorial ends it; so does loading another project ──
    {
        r.state.projectVersion = r.state.savedProjectVersion;
        r.help_start();
        assert(r.state.firstBeat.active);
        r.hold_tap(Button::R_SHIFT, Button::DPAD_UP);   // SONG → PROJECT
        assert(r.state.currentScreen == ScreenType::PROJECT);
        for (int i = 0; i < 12 && r.state.projectCursorRow != static_cast<int>(ProjectRow::PROJECT); ++i)
            r.tap(Button::DPAD_DOWN);
        while (r.state.projectCursorColumn < 4) r.tap(Button::DPAD_RIGHT);   // NEW
        r.tap(Button::A);
        assert(!r.state.firstBeat.active && r.host.project().name != "FIRSTBEAT2");

        r.help_start();   // again, then LOAD the first tutorial's file: another project
        assert(r.state.firstBeat.active);
        const std::string mine = r.state.firstBeat.projectName;
        r.hold_tap(Button::R_SHIFT, Button::DPAD_UP);
        for (int i = 0; i < 12 && r.state.projectCursorRow != static_cast<int>(ProjectRow::PROJECT); ++i)
            r.tap(Button::DPAD_DOWN);
        while (r.state.projectCursorColumn > 3) r.tap(Button::DPAD_LEFT);
        while (r.state.projectCursorColumn < 3) r.tap(Button::DPAD_RIGHT);   // LOAD
        r.tap(Button::A);
        for (int i = 0; i < 40; ++i) {
            const auto* item = r.state.fileBrowser.current();
            if (item && (item->displayName == "FIRSTBEAT" || item->displayName == "FIRSTBEAT.ptp")) break;
            r.tap(Button::DPAD_DOWN);
        }
        r.tap(Button::A);
        assert(r.host.project().name == "FIRSTBEAT" && mine != "FIRSTBEAT");
        assert(!r.state.firstBeat.active);
    }

    // ── A recording running: refused before any dialog, nothing changes ──────────────────────────
    {
        const std::string blob = songcore::serialize_project(r.host.project());
        r.state.capture.state = "recording";
        r.help_start();
        assert(!r.state.firstBeat.active && !r.state.confirm.is_open());
        assert(r.state.statusMessage.find("RECORDING") != std::string::npos);
        assert(songcore::serialize_project(r.host.project()) == blob);
        r.state.capture.state = "idle";
    }

    // ── Every FIRSTBEAT name taken: refused, the open project stays byte for byte ────────────────
    {
        Rig<FullFs> full(dir / "full");
        full.state.cursorColumn = 1;
        full.tap(Button::A);   // some work in the song…
        full.state.savedProjectVersion = full.state.projectVersion;   // …saved, so no confirm comes first
        const std::string blob = songcore::serialize_project(full.host.project());
        full.help_start();
        assert(!full.state.firstBeat.active && !full.state.confirm.is_open());
        assert(full.state.statusMessage.find("NO FREE PROJECT NAME") != std::string::npos);
        assert(songcore::serialize_project(full.host.project()) == blob);
    }

    // ── Settings: firstBeatDone round-trips; a file without it stays byte-identical ─────────────
    Theme theme = theme_default();
    StdFileSystem plainFs((dir / "settings-plain").string());
    assert(save_settings(plainFs, SettingsValues{}, theme));
    std::string plainJson;
    assert(plainFs.read_file(plainFs.settings_path(), plainJson));
    assert(plainJson.find("firstBeatDone") == std::string::npos);
    SettingsValues done;
    done.firstBeatDone = true;
    StdFileSystem settingsFs((dir / "settings").string());
    assert(save_settings(settingsFs, done, theme));
    SettingsValues back;
    Theme backTheme;
    assert(load_settings(settingsFs, back, backTheme) && back.firstBeatDone);

    std::cout << "ptfirst-beat-check: unsaved work asks first, ten steps ticked off by real buttons (chain into "
                 "the song, phrase into the chain, PHRASE, built-in synth, notes, START, change, delete, SAVE, LOAD), FIRSTBEAT2 beside an "
                 "existing FIRSTBEAT.ptp, L steps back and the card holds, starts from PROJECT and INSTRUMENT land on SONG, NEW and another LOAD end it, a phrase "
                 "switch is no change, an unsaved sample edit (said in the editor's own line), a running recording and exhausted names refuse without touching the project, START ends, firstBeatDone persists only when set, the empty-song invitation shows until done and hides with help OFF\n";
    return 0;
}
