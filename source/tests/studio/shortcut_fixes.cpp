// The shortcut fixes of 2026-11-27 (the shortcut audit): PERFORM's R+LEFT/RIGHT, the SNAP help, B+UP/DOWN
// on PHRASE into the next chain, undo / redo on SONG, CHAIN and TABLE, START from the cursor row on CHAIN
// and PHRASE, and PHRASE's L+B+A putting the copy into its chain. Portable, no assert: CI runs it on
// Linux and Windows. Usage: ptshortcut-fixes-check <scratch folder>
#include "audio-engine.h"
#include "songcore/host.h"
#include "ui/button_mapper.h"
#include "ui/help_text.h"
#include "ui/input_dispatcher.h"
#include "ui/navigation.h"
#include "ui/std_filesystem.h"

#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>

using namespace pt::ui;

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

struct Rig {
    std::unique_ptr<AudioEngine> engine = std::make_unique<AudioEngine>();
    songcore::SongcoreHost host{engine.get(), 48000};
    AppState state;
    StdFileSystem fs;
    InputDispatcher dispatch;
    MapperState mapper;
    uint64_t now = 1000;
    explicit Rig(const std::string& dir) : fs(dir), dispatch(state, host, fs) {
        host.new_project();
        state.project = &host.edit_project();
        state.settings.navSongRelative = true;
    }
    void send(Button b, ButtonAction a, ButtonMods m = ButtonMods{}) {
        now += 400;
        dispatch.set_now(static_cast<long long>(now));
        handle_button(ButtonEvent{b, a, m}, dispatch, mapper, now);
    }
    void tap(Button b) { send(b, ButtonAction::PRESSED); send(b, ButtonAction::RELEASED); }
    // Hold `held`, tap `b`, let go: a chord such as SELECT+UP or B+DOWN.
    void chord(Button held, Button b, ButtonMods m) {
        send(held, ButtonAction::PRESSED);
        send(b, ButtonAction::PRESSED, m);
        send(b, ButtonAction::RELEASED, m);
        send(held, ButtonAction::RELEASED);
    }
    void select_(Button b) { ButtonMods m; m.select = true; chord(Button::SELECT, b, m); }
    void b_(Button b) { ButtonMods m; m.b = true; chord(Button::B, b, m); }
};
}  // namespace

int main(int argc, char** argv) {
    const auto dir = std::filesystem::absolute(argc > 1 ? argv[1] : "ptshortcut-fixes").string();
    std::filesystem::create_directories(dir);

    // ── PERFORM: R+LEFT / R+RIGHT drop to the main screen of the column you came from ──
    {
        NavState s;
        s.currentScreen = ScreenType::PERFORM;
        s.previousColumn = 0;
        check(navigate_left(s).screen == ScreenType::SONG && navigate_right(s).screen == ScreenType::SONG,
              "PERFORM from column 0: R+LEFT and R+RIGHT go to SONG, not PHRASE");
        s.previousColumn = 4;
        check(navigate_right(s).screen == ScreenType::TABLE, "PERFORM from column 4: R+RIGHT goes to TABLE");
    }

    // ── SNAP: its own help on the sample editor's view-tools row ──
    check(detail::sample_editor_cell_topic(9, 3, 0) == HelpTopic::SE_SNAP, "SELECT on SNAP shows SNAP's help");

    // ── PHRASE: B+DOWN past the chain's last phrase goes on into the next chain ──
    {
        Rig r(dir + "/nav");
        auto& p = r.host.edit_project();
        p.tracks[0].chainRefs = {0, 1};
        p.chains[0].phraseRefs[0] = 0;
        p.chains[0].phraseRefs[1] = 1;
        p.chains[1].phraseRefs[0] = 2;
        r.state.currentScreen = ScreenType::PHRASE;
        r.state.songCursorRow = 0;
        r.state.songCursorColumn = 1;
        r.state.chainCursorRow = 1;
        r.state.currentChain = 0;
        r.state.currentPhrase = 1;
        r.b_(Button::DPAD_DOWN);
        check(r.state.currentChain == 1 && r.state.currentPhrase == 2 && r.state.songCursorRow == 1,
              "B+DOWN on the chain's last phrase: the next chain's first phrase");
        r.b_(Button::DPAD_UP);
        check(r.state.currentChain == 0 && r.state.currentPhrase == 1,
              "B+UP on the chain's first phrase: back to the previous chain's last phrase");
    }

    // ── Undo / redo on SONG, CHAIN and TABLE ──
    {
        Rig r(dir + "/undo");
        auto& p = r.host.edit_project();
        r.state.currentScreen = ScreenType::SONG;
        r.state.cursorRow = 0;
        r.state.cursorColumn = 1;
        r.tap(Button::A);   // a chain into the empty cell
        const int placed = songcore::chain_at(p, 0, 0);
        check(placed >= 0, "SONG: A puts a chain in");
        r.select_(Button::DPAD_UP);
        check(songcore::chain_at(p, 0, 0) < 0, "SONG: SELECT+UP undoes it");
        r.select_(Button::DPAD_DOWN);
        check(songcore::chain_at(p, 0, 0) == placed, "SONG: SELECT+DOWN redoes it");

        r.state.currentScreen = ScreenType::CHAIN;
        r.state.currentChain = placed;
        r.state.cursorRow = 2;
        r.state.cursorColumn = 1;
        const int before = p.chains[static_cast<size_t>(placed)].phraseRefs[2];
        r.tap(Button::A);
        const int after = p.chains[static_cast<size_t>(placed)].phraseRefs[2];
        r.select_(Button::DPAD_UP);
        check(after != before && p.chains[static_cast<size_t>(placed)].phraseRefs[2] == before,
              "CHAIN: an inserted phrase is undone");

        r.state.currentScreen = ScreenType::TABLE;
        r.state.currentTable = 3;
        r.state.tableCursorRow = 0;
        r.state.tableCursorColumn = 1;
        const std::string tableBefore = songcore::serialize_project(p);
        { ButtonMods a; a.a = true; r.chord(Button::A, Button::DPAD_RIGHT, a); }   // A+RIGHT: a value edit
        const bool changed = songcore::serialize_project(p) != tableBefore;
        r.select_(Button::DPAD_UP);
        check(changed && songcore::serialize_project(p) == tableBefore, "TABLE: an edit is undone");
    }

    // ── START from the cursor row on PHRASE and CHAIN ──
    {
        Rig r(dir + "/play");
        auto& p = r.host.edit_project();
        p.tracks[0].chainRefs = {0};
        p.chains[0].phraseRefs[0] = 0;
        p.chains[0].phraseRefs[3] = 1;
        for (auto& st : p.phrases[0].steps) st.note = songcore::Note::C4();
        for (auto& st : p.phrases[1].steps) st.note = songcore::Note::C4();
        r.state.currentScreen = ScreenType::PHRASE;
        r.state.currentChain = 0;
        r.state.currentPhrase = 0;
        r.state.cursorRow = 9;
        r.tap(Button::START);
        const auto ph = r.host.playheads(0);
        check(r.host.is_playing() && ph.phraseStep == 9, "PHRASE: START plays from the cursor's step (" +
                                                             std::to_string(ph.phraseStep) + ")");
        r.tap(Button::START);
        r.state.currentScreen = ScreenType::CHAIN;
        r.state.cursorRow = 3;
        r.state.cursorColumn = 1;
        r.tap(Button::START);
        const auto ch = r.host.playheads(0);
        check(r.host.is_playing() && ch.chainRow == 3, "CHAIN: START plays from the cursor's row (" +
                                                           std::to_string(ch.chainRow) + ")");
        r.tap(Button::START);
    }

    // ── PHRASE L+B+A: the chain row now plays the copy ──
    {
        Rig r(dir + "/clone");
        auto& p = r.host.edit_project();
        p.tracks[0].chainRefs = {0};
        p.chains[0].phraseRefs[0] = 0;
        p.phrases[0].steps[0].note = songcore::Note::C4();
        r.state.currentScreen = ScreenType::PHRASE;
        r.state.songCursorRow = 0;
        r.state.songCursorColumn = 1;
        r.state.chainCursorRow = 0;
        r.state.currentChain = 0;
        r.state.currentPhrase = 0;
        {   // L+B+A: hold L, then B, then press A
            ButtonMods l; l.l = true;
            ButtonMods lb = l; lb.b = true;
            r.send(Button::L_SHIFT, ButtonAction::PRESSED);
            r.send(Button::B, ButtonAction::PRESSED, l);
            r.send(Button::A, ButtonAction::PRESSED, lb);
            r.send(Button::A, ButtonAction::RELEASED, lb);
            r.send(Button::B, ButtonAction::RELEASED, l);
            r.send(Button::L_SHIFT, ButtonAction::RELEASED);
        }
        check(r.state.currentPhrase != 0 && p.chains[0].phraseRefs[0] == r.state.currentPhrase,
              "PHRASE L+B+A: the chain row plays the copy you are now editing");
    }

    std::filesystem::remove_all(dir);
    std::printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
