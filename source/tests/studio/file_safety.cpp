// SPDX-License-Identifier: GPL-3.0-or-later
// ptfile-safety-check: the 1.2.1 extra review's findings, each through the real buttons
// (studio_button, then handle_button) and the real StdFileSystem:
//   E1 LOAD over unsaved work asks; NO keeps the work and its autosave
//   E2 SAVE never replaces another document's file unasked (REPLACE / NEW COPY / CANCEL)
//   E3 a folder is never copied or moved into itself
//   E4 A · D-pad · A within 300 ms is two A presses, not a lost double-tap
//   E5 a case-only rename works, and an extension in another case is not doubled
//   E6 an instrument preset's FILE name is sanitised like every other preset's
//   E7 COLLECT keeps two different files of one name apart (kick.wav, kick_2.wav)
//   B1 SAVE keeps the previous versions (Backups/<NAME>_bak1..5.ptp), also what REPLACE overwrites
//   M1 EXPORT > MIDI writes <Renders>/<NAME>_NNNN.mid through the real button
//   P1 Presets/Inbox installs dropped presets, keeps both on a name clash, leaves what it cannot read
//   S1 the synced write (temp, fsync, rename) is correct, replaces cleanly and fails without a trace
// usage: ptfile-safety-check <new run directory>
#include "audio-engine.h"
#include "songcore/host.h"
#include "songcore/project_io.h"
#include "ui/button_mapper.h"
#include "ui/folder_config.h"
#include "ui/instrument_row_layout.h"
#include "ui/input_dispatcher.h"
#include "ui/project_actions.h"
#include "ui/settings_row_layout.h"
#include "ui/std_filesystem.h"
#include "ui/studio.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <functional>
#include <vector>
#if !defined(_WIN32)
#include <unistd.h>
#endif
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>

using namespace pt::ui;
namespace fs = std::filesystem;

namespace {

// Does this folder's filesystem tell "Probe" from "probe"? Linux (ext4, the ARM handhelds' own
// partitions) does; FAT cards, macOS and Windows do not. Expectations follow what is really there.
bool case_sensitive(const fs::path& dir) {
    fs::create_directories(dir);
    const fs::path upper = dir / "CaseProbe.tmp";
    { std::ofstream(upper) << "x"; }
    const bool sensitive = !fs::exists(dir / "caseprobe.tmp");
    fs::remove(upper);
    return sensitive;
}

struct Rig {
    std::unique_ptr<AudioEngine> engine = std::make_unique<AudioEngine>();
    songcore::SongcoreHost host{engine.get(), 48000};
    AppState state;
    StdFileSystem sfs;
    InputDispatcher d;
    MapperState mapper;
    uint64_t now = 1000;
    ButtonMods held;

    explicit Rig(const fs::path& dir) : sfs((dir / "data").string()), d(state, host, sfs) {
        assert(host.push_project(songcore::serialize_project(songcore::make_default_project())));
        state.project = &host.edit_project();
        state.currentScreen = ScreenType::PROJECT;
        tick(0);
    }
    void tick(uint64_t ms) { now += ms; d.set_now(static_cast<long long>(now)); }
    void set(Button b, bool v) {
        if (b == Button::A) held.a = v;
        if (b == Button::B) held.b = v;
        if (b == Button::L_SHIFT) held.l = v;
    }
    void send(Button b, ButtonAction a) {
        tick(40);
        const ButtonEvent e{b, a, held};
        if (!studio_button(e, state, host, *engine, sfs)) handle_button(e, d, mapper, now);
    }
    void press(Button b) { set(b, true); send(b, ButtonAction::PRESSED); }
    void release(Button b) { set(b, false); send(b, ButtonAction::RELEASED); }
    // A human pause first, so no A here is read as the second half of an A,A.
    void tap(Button b) { if (b == Button::A) tick(400); press(b); release(b); }
    void quick(Button b) { press(b); release(b); }
    void cell(ProjectRow row, int column) {
        state.currentScreen = ScreenType::PROJECT;
        state.projectCursorRow = static_cast<int>(row);
        state.projectCursorColumn = column;
    }
    void tempo_up(int n) {
        cell(ProjectRow::TEMPO, 1);
        tick(400);
        press(Button::A);
        for (int i = 0; i < n; ++i) quick(Button::DPAD_RIGHT);
        release(Button::A);
    }
    int file_tempo(const std::string& path) {
        std::string text;
        if (!sfs.read_file(path, text)) return -1;
        return songcore::parse_project(songcore::json::parse(text)).tempo;
    }
    int item(const std::string& path) const {
        for (size_t i = 0; i < state.fileBrowser.items.size(); ++i)
            if (state.fileBrowser.items[i].path == path) return static_cast<int>(i);
        return -1;
    }
    void focus(const std::string& path, bool fast = false) {
        const int target = item(path);
        assert(target >= 0);
        for (int i = 0; state.fileBrowser.cursor != target && i < 200; ++i)
            fast ? quick(target > state.fileBrowser.cursor ? Button::DPAD_DOWN : Button::DPAD_UP)
                 : tap(target > state.fileBrowser.cursor ? Button::DPAD_DOWN : Button::DPAD_UP);
        assert(state.fileBrowser.cursor == target);
    }
};

void load_asks(const fs::path& root) {
    Rig r(root / "e1");
    r.host.edit_project().name = "KEEP";
    r.cell(ProjectRow::PROJECT, 1);
    r.tap(Button::A);
    const std::string keep = r.sfs.projects_directory() + "/KEEP.ptp";
    assert(r.file_tempo(keep) == 128 && !r.state.project_dirty());

    // Clean: LOAD goes straight to the browser, as before.
    r.cell(ProjectRow::PROJECT, 3);
    r.tap(Button::A);
    assert(r.state.currentScreen == ScreenType::FILE_BROWSER && !r.state.confirm.is_open());
    r.tap(Button::B);

    r.tempo_up(30);
    const int unsaved = r.host.project().tempo;
    r.tick(3500);   // the autosave's debounce
    assert(unsaved == 158 && r.state.project_dirty() && fs::exists(r.sfs.autosave_file_path()));

    // Dirty: the question first; NO keeps everything.
    r.cell(ProjectRow::PROJECT, 3);
    r.tap(Button::A);
    assert(r.state.confirm.kind == ConfirmDialogState::Kind::LOAD_PROJECT);
    assert(r.state.currentScreen == ScreenType::PROJECT);
    r.tap(Button::B);
    assert(!r.state.confirm.is_open() && r.state.currentScreen == ScreenType::PROJECT);
    assert(r.host.project().tempo == unsaved && fs::exists(r.sfs.autosave_file_path()));

    // YES opens the browser; the load then replaces the work, as it always did.
    r.tap(Button::A);
    assert(r.state.confirm.kind == ConfirmDialogState::Kind::LOAD_PROJECT);
    r.tap(Button::A);
    assert(!r.state.confirm.is_open() && r.state.currentScreen == ScreenType::FILE_BROWSER);
    r.focus(keep);
    r.tap(Button::A);
    assert(r.state.statusMessage == "LOADED" && r.host.project().tempo == 128);
    assert(!fs::exists(r.sfs.autosave_file_path()) && !r.state.project_dirty());
}

void save_asks(const fs::path& root) {
    Rig r(root / "e2");
    const std::string untitled = r.sfs.projects_directory() + "/UNTITLED.ptp";
    r.tempo_up(5);
    r.cell(ProjectRow::PROJECT, 1);
    r.tap(Button::A);
    assert(!r.state.confirm.is_open() && r.file_tempo(untitled) == 133);

    // Its own file again: one press, no question.
    r.tempo_up(1);
    r.cell(ProjectRow::PROJECT, 1);
    r.tap(Button::A);
    assert(!r.state.confirm.is_open() && r.file_tempo(untitled) == 134);

    // A second, unnamed project: SAVE asks, the cursor on CANCEL — a double press decides nothing.
    r.cell(ProjectRow::PROJECT, 4);
    r.tap(Button::A);   // NEW (clean)
    r.tempo_up(20);
    r.cell(ProjectRow::PROJECT, 1);
    r.tap(Button::A);
    assert(r.state.confirm.kind == ConfirmDialogState::Kind::SAVE_EXISTS);
    assert(r.state.confirm.arg == ConfirmDialogState::COLLECT_CANCEL);
    r.tap(Button::A);   // CANCEL
    assert(!r.state.confirm.is_open() && r.file_tempo(untitled) == 134 && r.state.project_dirty());

    // NEW COPY: a free name, the first project untouched.
    r.cell(ProjectRow::PROJECT, 1);
    r.tap(Button::A);
    r.tap(Button::DPAD_LEFT);   // CANCEL → NEW COPY
    r.tap(Button::A);
    const std::string copy = r.sfs.projects_directory() + "/UNTITLED_2.ptp";
    assert(r.host.project().name == "UNTITLED_2" && r.file_tempo(copy) == 148);
    assert(r.file_tempo(untitled) == 134 && !r.state.project_dirty());

    // REPLACE: what the user chose, explicitly.
    r.cell(ProjectRow::PROJECT, 4);
    r.tap(Button::A);
    r.tempo_up(2);
    r.cell(ProjectRow::PROJECT, 1);
    r.tap(Button::A);
    r.tap(Button::DPAD_LEFT);
    r.tap(Button::DPAD_LEFT);   // → REPLACE
    r.tap(Button::A);
    assert(r.file_tempo(untitled) == 130 && !r.state.project_dirty());

    // A loaded file re-saves without a question, whatever case the name has on disk.
    r.cell(ProjectRow::PROJECT, 3);
    r.tap(Button::A);
    r.focus(copy);
    r.tap(Button::A);
    assert(r.state.statusMessage == "LOADED");
    r.tempo_up(1);
    r.cell(ProjectRow::PROJECT, 1);
    r.tap(Button::A);
    assert(!r.state.confirm.is_open() && r.file_tempo(copy) == 149);

    // The free name keeps the NAME row's 20 characters.
    r.host.edit_project().name = "ABCDEFGHIJKLMNOPQRST";
    r.state.projectPath.clear();
    assert(r.sfs.write_file(r.sfs.projects_directory() + "/ABCDEFGHIJKLMNOPQRST.ptp", "{}"));
    assert(free_project_name(r.sfs, r.host.project().name) == "ABCDEFGHIJKLMNOPQR_2");
}

// SAVE after renaming the loaded "Song" to "SONG": a separate SONG.ptp (Linux) must ask; on a
// case-folding filesystem SONG.ptp IS this song's own file and saves at once.
void save_case_identity(const fs::path& root) {
    Rig r(root / "e2-case");
    r.host.edit_project().name = "Song";
    r.cell(ProjectRow::PROJECT, 1);
    r.tap(Button::A);
    const std::string own = r.sfs.projects_directory() + "/Song.ptp";
    const std::string upper = r.sfs.projects_directory() + "/SONG.ptp";
    assert(r.state.projectPath == own && fs::exists(own));
    const bool sensitive = case_sensitive(r.sfs.projects_directory());
    if (sensitive) {
        auto other = songcore::make_default_project();
        other.name = "SONG";
        other.tempo = 99;
        assert(r.sfs.write_file(upper, songcore::serialize_project(other)));
    }
    r.host.edit_project().name = "SONG";
    r.tempo_up(3);
    r.cell(ProjectRow::PROJECT, 1);
    r.tap(Button::A);
    if (sensitive) {
        assert(r.state.confirm.kind == ConfirmDialogState::Kind::SAVE_EXISTS);
        r.tap(Button::A);   // CANCEL
        assert(r.file_tempo(upper) == 99 && r.file_tempo(own) == 128);
    } else {
        assert(!r.state.confirm.is_open() && r.file_tempo(upper) == 131);
    }
}

void no_paste_into_itself(const fs::path& root) {
    Rig r(root / "e3");
    const std::string samples = r.sfs.samples_directory();
    const std::string kit = samples + "/Kit";
    fs::create_directories(kit + "/Sub");
    std::ofstream(kit + "/kick.wav") << "KICK";
    std::ofstream(kit + "/Sub/snare.wav") << "SNARE";
    fs::create_directories(samples + "/Other");

    // Filesystem level: refused, nothing created.
    assert(!r.sfs.copy_file(kit, kit + "/Kit") && !r.sfs.move_file(kit, kit + "/Sub/Kit"));
    assert(!fs::exists(kit + "/Kit") && !fs::exists(kit + "/Sub/Kit") && fs::exists(kit + "/kick.wav"));
    assert(path_is_within(kit + "/Kit", kit) && path_is_within(kit, kit) && path_is_within("C:\\X\\kit\\a", "c:/x/KIT"));
    assert(!path_is_within(samples + "/Kit2", kit) && !path_is_within(samples, kit));

    // A real copy still works.
    assert(r.sfs.copy_file(kit, samples + "/Other/Kit") && fs::exists(samples + "/Other/Kit/Sub/snare.wav"));
    assert(r.sfs.move_file(samples + "/Other/Kit", samples + "/Moved") && fs::exists(samples + "/Moved/kick.wav"));
    assert(!fs::exists(samples + "/Other/Kit"));

    // Through the browser: CUT Kit, open Kit, paste → FAILED, no nested folder.
    r.d.open_file_browser(AppState::BrowserPurpose::LOAD_PROJECT, kit, {});
    assert(r.state.currentScreen == ScreenType::FILE_BROWSER && r.state.fileBrowser.currentDirectory == kit);
    r.state.fileBrowser.fileClipboard = {kit};   // as L+B · L+A on Kit one folder up would leave it
    r.state.fileBrowser.fileClipboardIsCut = true;
    r.press(Button::L_SHIFT);
    r.tap(Button::A);   // L+A: paste
    r.release(Button::L_SHIFT);
    assert(!fs::exists(kit + "/Kit") && fs::exists(kit + "/kick.wav"));
    assert(r.state.fileBrowser.statusMessage.find("FAILED 1") != std::string::npos);
}

void rename_cases(const fs::path& root) {
    const fs::path dir = root / "e5";
    fs::create_directories(dir);
    StdFileSystem sfs(dir.string());
    const auto make = [&](const char* name) { std::ofstream(dir / name) << name; return (dir / name).generic_string(); };
    const auto names = [&] {
        std::string out;
        for (const auto& e : fs::directory_iterator(dir)) out += e.path().filename().string() + " ";
        return out;
    };

    const std::string song = make("song.ptp");
    assert(sfs.rename_file(song, "SONG"));
    assert(names() == "SONG.ptp ");
    fs::remove(dir / "SONG.ptp");

    const std::string kick = make("Kick.WAV");
    assert(sfs.rename_file(kick, "Snare.wav") && fs::exists(dir / "Snare.wav") && !fs::exists(dir / "Snare.wav.WAV"));
    fs::remove(dir / "Snare.wav");

    // Never onto another file. "b" is that file; "B" is it too where the filesystem folds case, and a
    // different, free name where it does not (Linux) — then the rename is allowed and b.ptp stays.
    const std::string a = make("a.ptp");
    make("b.ptp");
    assert(!sfs.rename_file(a, "b") && fs::exists(dir / "a.ptp"));
    if (case_sensitive(dir)) {
        assert(sfs.rename_file(a, "B") && fs::exists(dir / "B.ptp") && !fs::exists(dir / "a.ptp"));
        std::ifstream b(dir / "b.ptp");
        std::string kept;
        b >> kept;
        assert(kept == "b.ptp");   // the other file untouched
    } else {
        assert(!sfs.rename_file(a, "B") && fs::exists(dir / "a.ptp"));
    }
}

void fast_a_after_a_move(const fs::path& root) {
    // The browser: A on a folder, DOWN, A on a song within 300 ms → the song loads.
    {
        Rig r(root / "e4-browser");
        r.host.edit_project().name = "SUBSONG";
        const std::string sub = r.sfs.projects_directory() + "/Sub";
        assert(r.sfs.write_file(sub + "/SUBSONG.ptp", songcore::serialize_project(r.host.project())));
        r.cell(ProjectRow::PROJECT, 3);
        r.tap(Button::A);
        r.focus(sub);
        r.tap(Button::A);
        assert(r.state.fileBrowser.currentDirectory == sub);
        const uint64_t first = r.now - 40;   // the folder's A press
        r.focus(sub + "/SUBSONG.ptp", /*fast=*/true);
        r.quick(Button::A);
        assert(r.now - 80 - first < 300);
        assert(r.state.currentScreen != ScreenType::FILE_BROWSER && r.state.statusMessage == "LOADED");
    }
    // PHRASE: A · DOWN at 40 ms per event (A-to-A 160 ms) enters a note on every row. Before the fix
    // every second A arrived as A_A on another row and was dropped: 4 notes of 8.
    {
        Rig r(root / "e4-phrase");
        auto p = r.host.project();
        p.tracks[0].chainRefs = {0};
        p.chains[0].phraseRefs[0] = 0;
        assert(r.host.push_project(songcore::serialize_project(p)));
        r.state.project = &r.host.edit_project();
        r.state.currentScreen = ScreenType::PHRASE;
        r.state.currentPhrase = 0;
        r.state.cursorRow = 0;
        r.state.cursorColumn = 1;
        r.tick(500);
        for (int n = 0; n < 8; ++n) { r.quick(Button::A); r.quick(Button::DPAD_DOWN); }
        int notes = 0;
        for (const auto& step : r.host.project().phrases[0].steps) notes += songcore::step_has_note(step) ? 1 : 0;
        assert(notes == 8);
    }
    // Tap tempo keeps its A,A on one cell.
    {
        Rig r(root / "e4-tap");
        r.cell(ProjectRow::TEMPO, 2);
        r.tap(Button::A);
        for (int i = 0; i < 5; ++i) { r.tick(210); r.quick(Button::A); }
        // 250 ms between taps (210 + 40) = 240 BPM, within the tempo range.
        assert(r.host.project().tempo != 128);
    }
}

void preset_file_names(const fs::path& root) {
    for (const char* name : {"../../ESCAPE", "KITS/808", "BASS?:1"}) {
        const fs::path dir = root / "e6" / std::to_string(std::hash<std::string>{}(name) % 100000);
        Rig r(dir);
        r.host.edit_project().instruments[0].name = name;
        r.state.currentInstrument = 0;
        const auto type = r.host.project().instruments[0].instrumentType;
        int row = -1, col = -1;
        for (int rr = 0; rr < 40 && row < 0; ++rr)
            for (int cc = 0; cc < 12; ++cc)
                if (instrument_cell_at(type, rr, cc) == InstrumentCell::PRESET_SAVE) { row = rr; col = cc; break; }
        assert(row >= 0);
        r.state.currentScreen = ScreenType::INSTRUMENT;
        r.state.instrumentCursorRow = row;
        r.state.instrumentCursorColumn = col;
        r.tap(Button::A);
        assert(r.state.qwerty.isOpen);
        r.tap(Button::START);   // APPLY with the instrument's name
        assert(r.state.statusMessage == std::string("SAVED: ") + name);
        std::vector<std::string> found;
        for (const auto& e : fs::recursive_directory_iterator(dir / "data"))
            if (e.path().extension() == ".pti") found.push_back(fs::relative(e.path(), dir / "data").generic_string());
        assert(found.size() == 1);
        assert(found[0] == "Presets/Instruments/" + safe_file_name(name) + ".pti");
    }
}

void collect_keeps_namesakes(const fs::path& root) {
    const fs::path home = root / "e7" / "TrackWerk";
    const auto wav = [](const fs::path& p, int16_t value) {
        fs::create_directories(p.parent_path());
        std::ofstream f(p, std::ios::binary);
        const auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
        const auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
        f.write("RIFF", 4); u32(44); f.write("WAVE", 4);
        f.write("fmt ", 4); u32(16); u16(1); u16(1); u32(48000); u32(96000); u16(2); u16(16);
        f.write("data", 4); u32(8);
        for (int i = 0; i < 4; ++i) f.write(reinterpret_cast<const char*>(&value), 2);
    };
    const auto first_sample = [](const fs::path& p) {
        std::ifstream f(p, std::ios::binary);
        char b[46] = {};
        f.read(b, sizeof(b));
        int16_t v = 0;
        std::memcpy(&v, b + 44, 2);
        return v;
    };
    wav(home / "Songs/SongA/Samples/kick.wav", 1111);
    wav(home / "Songs/SongB/Samples/kick.wav", 2222);
    auto engine = std::make_unique<AudioEngine>();
    songcore::SongcoreHost host{engine.get(), 48000};
    StdFileSystem sfs(home.string());
    auto p = songcore::make_default_project();
    p.name = "MIX";
    p.instruments[0].sampleFilePath = (home / "Songs/SongA/Samples/kick.wav").generic_string();
    p.instruments[1].sampleFilePath = (home / "Songs/SongB/Samples/kick.wav").generic_string();
    p.instruments[2].sampleFilePath = (home / "Songs/SongA/Samples/kick.wav").generic_string();   // shared
    assert(host.push_project(songcore::serialize_project(p)));
    const ActionResult r = collect_project(host, sfs, "MIX", {});
    assert(r.ok);
    std::string text;
    assert(sfs.read_file((home / "Songs/MIX/MIX.ptp").string(), text));
    const auto q = songcore::parse_project(songcore::json::parse(text));
    assert(*q.instruments[0].sampleFilePath == "Samples/kick.wav");
    assert(*q.instruments[1].sampleFilePath == "Samples/kick_2.wav");
    assert(*q.instruments[2].sampleFilePath == "Samples/kick.wav");   // one file, one copy
    assert(first_sample(home / "Songs/MIX/Samples/kick.wav") == 1111);
    assert(first_sample(home / "Songs/MIX/Samples/kick_2.wav") == 2222);

    // R1: collected again into MIX. Instrument 0 already plays MIX's own Samples/kick.wav; a
    // namesake from elsewhere, met FIRST in instrument order, must not be copied over it.
    {
        const fs::path h = root / "e7-again" / "TrackWerk";
        wav(h / "Songs/MIX/Samples/kick.wav", 1111);     // FIRST, already in the song folder
        wav(h / "Songs/Other/Samples/kick.wav", 2222);   // SECOND, a namesake
        StdFileSystem rfs(h.string());
        auto m = songcore::make_default_project();
        m.name = "MIX";
        m.instruments[0].sampleFilePath = (h / "Songs/Other/Samples/kick.wav").generic_string();
        m.instruments[1].sampleFilePath = (h / "Songs/MIX/Samples/kick.wav").generic_string();
        songcore::SongcoreHost again{engine.get(), 48000};
        assert(again.push_project(songcore::serialize_project(m)));
        const ActionResult ra = collect_project(again, rfs, "MIX", {});
        assert(ra.ok);
        std::string t;
        assert(rfs.read_file((h / "Songs/MIX/MIX.ptp").string(), t));
        const auto a = songcore::parse_project(songcore::json::parse(t));
        assert(*a.instruments[1].sampleFilePath == "Samples/kick.wav");
        assert(*a.instruments[0].sampleFilePath == "Samples/kick_2.wav");
        assert(first_sample(h / "Songs/MIX/Samples/kick.wav") == 1111);
        assert(first_sample(h / "Songs/MIX/Samples/kick_2.wav") == 2222);
    }

    // R2: Samples/Kick.wav and Samples/kick.wav are two files where the filesystem tells case apart.
    // Both survive, under names that also stay apart on a FAT card.
    {
        const fs::path h = root / "e7-case" / "TrackWerk";
        wav(h / "Samples/Kick.wav", 3333);
        wav(h / "Samples/kick.wav", 4444);
        const bool two = first_sample(h / "Samples/Kick.wav") == 3333;
        StdFileSystem cfs(h.string());
        auto m = songcore::make_default_project();
        m.name = "CASES";
        m.instruments[0].sampleFilePath = (h / "Samples/Kick.wav").generic_string();
        m.instruments[1].sampleFilePath = (h / "Samples/kick.wav").generic_string();
        songcore::SongcoreHost cases{engine.get(), 48000};
        assert(cases.push_project(songcore::serialize_project(m)));
        const ActionResult rc = collect_project(cases, cfs, "CASES", {});
        assert(rc.ok);
        std::string t;
        assert(cfs.read_file((h / "Songs/CASES/CASES.ptp").string(), t));
        const auto c = songcore::parse_project(songcore::json::parse(t));
        const std::string p0 = *c.instruments[0].sampleFilePath, p1 = *c.instruments[1].sampleFilePath;
        if (two) {
            assert(p0 == "Samples/Kick.wav" && p1 == "Samples/kick_2.wav");
            assert(first_sample(h / "Songs/CASES" / p0) == 3333 && first_sample(h / "Songs/CASES" / p1) == 4444);
        } else {
            // One file under two spellings: one copy, both instruments on it.
            assert(p0 == p1 && first_sample(h / "Songs/CASES" / p0) == 4444);
        }
        std::cout << "collect case variants: " << (two ? "two files kept apart" : "one file (case-folding volume)") << "\n";
    }
}

// Presets dropped into Presets/Inbox are installed where the app looks; nothing is overwritten and
// nothing unreadable is lost.
void inbox_case(const fs::path& root) {
    const fs::path dir = root / "p1";
    StdFileSystem sfs(dir.string());
    auto project = songcore::make_default_project();
    const std::string preset = songcore::serialize_instrument_preset(songcore::make_instrument_preset(project, 0));
    const std::string inbox = sfs.make_app_folder("Presets/Inbox");
    assert(fs::is_directory(inbox));   // the place to drop files exists from the first start
    const auto drop = [&](const char* name, const std::string& text) { assert(sfs.write_file(inbox + "/" + name, text)); };
    drop("Fresh.pti", preset);
    drop("Broken.pti", "this is not a preset");
    drop("Clash.pti", preset);
    drop("Upper.PTI", preset);
    drop("room.ptr", "{}");
    drop("echo.ptd", "{}");
    drop("minor.pts", "{}");
    drop("swing.ptg", "{}");
    drop("notes.txt", "hello");
    drop(".hidden.pti", preset);
    drop("array.ptg", "[1,2]");
    assert(sfs.write_file(sfs.instruments_directory() + "/Clash.pti", "OLD"));   // a name already taken

    int refused = -1;
    const int installed = import_preset_inbox(sfs, &refused);
    assert(installed == 7 && refused == 2);   // Fresh, Clash_2, Upper (any case), room, echo, minor, swing; Broken and array refused
    const auto text = [&](const std::string& p) { std::string t; return sfs.read_file(p, t) ? t : std::string("<missing>"); };
    assert(text(sfs.instruments_directory() + "/Fresh.pti") == preset && text(sfs.instruments_directory() + "/Upper.PTI") == preset);
    assert(text(sfs.instruments_directory() + "/Clash.pti") == "OLD");               // never overwritten
    assert(text(sfs.instruments_directory() + "/Clash_2.pti") == preset);            // both kept
    assert(sfs.file_exists(sfs.reverbs_directory() + "/room.ptr") && sfs.file_exists(sfs.delays_directory() + "/echo.ptd"));
    assert(sfs.file_exists(sfs.scales_directory() + "/minor.pts") && sfs.file_exists(sfs.grooves_directory() + "/swing.ptg"));
    assert(sfs.file_exists(inbox + "/Broken.pti") && sfs.file_exists(inbox + "/array.ptg") &&
           sfs.file_exists(inbox + "/notes.txt") && sfs.file_exists(inbox + "/.hidden.pti"));   // left alone
    assert(!sfs.file_exists(inbox + "/Fresh.pti"));
    int again = -1;
    assert(import_preset_inbox(sfs, &again) == 0 && again == 2);                 // nothing moves twice
}

// EXPORT > MIDI through the button: a file in Renders, a numbered name each time, and a message when
// there is nothing to export.
void midi_button_case(const fs::path& root) {
    Rig r(root / "m1");
    r.cell(ProjectRow::EXPORT, 3);
    r.tap(Button::A);
    assert(r.state.statusMessage == "NO NOTES TO EXPORT" && !fs::exists(r.sfs.renders_directory() + "/UNTITLED_0001.mid"));
    auto p = r.host.project();
    p.tracks[0].chainRefs = {0};
    p.chains[0].phraseRefs[0] = 0;
    p.phrases[0].steps[0].note = songcore::Note::C4();
    p.phrases[0].steps[4].note = songcore::Note{7, 4};
    p.phrases[0].steps[4].fx1Type = 3;
    assert(r.host.push_project(songcore::serialize_project(p)));
    r.state.project = &r.host.edit_project();
    r.cell(ProjectRow::EXPORT, 3);
    r.tap(Button::A);
    assert(r.state.statusMessage == "MIDI: 2 NOTES, 1 TRACK (FX NOT INCLUDED)");
    r.tap(Button::A);
    const std::string first = r.sfs.renders_directory() + "/UNTITLED_0001.mid", second = r.sfs.renders_directory() + "/UNTITLED_0002.mid";
    std::string a, b;
    assert(r.sfs.read_file(first, a) && r.sfs.read_file(second, b) && a == b && a.compare(0, 4, "MThd") == 0);
}

// SAVE keeps what it replaces, newest first, up to five, and never the same version twice.
void backup_cases(const fs::path& root) {
    Rig r(root / "b1");
    const std::string file = r.sfs.projects_directory() + "/UNTITLED.ptp";
    const auto bak = [&](int n) { return project_backup_path(r.sfs, "UNTITLED", n); };
    const auto save_tempo = [&](int tempoUps) {
        r.tempo_up(tempoUps);
        r.cell(ProjectRow::PROJECT, 1);
        r.tap(Button::A);
        assert(!r.state.confirm.is_open() && r.state.statusMessage == "SAVED");
    };
    save_tempo(1);                                   // 129: nothing to keep yet
    assert(!fs::exists(bak(1)) && r.file_tempo(file) == 129);
    save_tempo(1);                                   // 130: 129 is kept
    assert(r.file_tempo(bak(1)) == 129 && r.file_tempo(file) == 130 && !fs::exists(bak(2)));
    r.cell(ProjectRow::PROJECT, 1);                  // saved again without a change: nothing new
    r.tap(Button::A);
    assert(r.file_tempo(bak(1)) == 129 && !fs::exists(bak(2)));
    for (int i = 0; i < 6; ++i) save_tempo(1);       // 131 … 136
    assert(r.file_tempo(file) == 136);
    for (int n = 1; n <= kProjectBackupsKept; ++n) assert(r.file_tempo(bak(n)) == 136 - n);   // newest first
    assert(!fs::exists(bak(kProjectBackupsKept + 1)));

    // REPLACE keeps the other project it overwrites, and the backup holds that project's own tempo.
    Rig q(root / "b2");
    q.tempo_up(5);
    q.cell(ProjectRow::PROJECT, 1);
    q.tap(Button::A);
    const std::string untitled = q.sfs.projects_directory() + "/UNTITLED.ptp";
    q.cell(ProjectRow::PROJECT, 4);
    q.tap(Button::A);                                // NEW
    q.tempo_up(20);
    q.cell(ProjectRow::PROJECT, 1);
    q.tap(Button::A);                                // asks
    assert(q.state.confirm.kind == ConfirmDialogState::Kind::SAVE_EXISTS);
    q.tap(Button::DPAD_LEFT);
    q.tap(Button::DPAD_LEFT);                        // REPLACE
    q.tap(Button::A);
    assert(q.file_tempo(untitled) == 148 && q.file_tempo(project_backup_path(q.sfs, "UNTITLED", 1)) == 133);
}

// The write behind SAVE, settings and the autosave: temp file, fsync, rename, directory sync. A power cut
// cannot be staged here; what can be checked is that the result is exact and that a failure leaves the
// old file whole and no temp behind.
void write_cases(const fs::path& root) {
    const fs::path dir = root / "s1";
    StdFileSystem sfs(dir.string());
    const auto read = [&](const std::string& path) {
        std::string text;
        assert(sfs.read_file(path, text));
        return text;
    };
    const std::string a = (dir / "sub/a.ptp").generic_string();   // the parent folder does not exist yet
    assert(sfs.write_file(a, "one") && read(a) == "one");
    assert(sfs.write_file(a, "2") && read(a) == "2");             // shorter: no tail of the old content
    assert(sfs.write_file(a, "") && fs::exists(a) && fs::file_size(a) == 0);
    std::string big(3 << 20, 'x');
    for (size_t i = 0; i < big.size(); i += 4099) big[i] = static_cast<char>('a' + i % 26);
    assert(sfs.write_file(a, big) && read(a) == big);
    assert(sfs.write_bytes(a, big.data(), big.size() / 2) && read(a) == big.substr(0, big.size() / 2));
    assert(sfs.write_file_relaxed(a, "relaxed") && read(a) == "relaxed");
    assert(!fs::exists(a + ".tmp"));

    // The target is a folder: the rename cannot succeed. False, the folder intact, no temp.
    const std::string folder = (dir / "folder.ptp").generic_string();
    fs::create_directories(folder + "/keep");
    assert(!sfs.write_file(folder, "x") && fs::is_directory(folder + "/keep") && !fs::exists(folder + ".tmp"));

#if !defined(_WIN32)
    // A folder that refuses new files: false for both writes, the old file whole, no temp.
    if (geteuid() != 0) {
        fs::permissions(dir / "sub", fs::perms::owner_read | fs::perms::owner_exec);
        const bool durable = sfs.write_file(a, "lost"), relaxed = sfs.write_file_relaxed(a, "lost");
        fs::permissions(dir / "sub", fs::perms::owner_all);
        assert(!durable && !relaxed && read(a) == "relaxed" && !fs::exists(a + ".tmp"));
    }
#endif
}

}  // namespace

int main(int argc, char** argv) {
    assert(argc == 2);
    const fs::path root = fs::absolute(argv[1]);
    assert(!fs::exists(root) || fs::is_empty(root));
    fs::create_directories(root);
    load_asks(root);
    save_asks(root);
    save_case_identity(root);
    no_paste_into_itself(root);
    rename_cases(root);
    fast_a_after_a_move(root);
    preset_file_names(root);
    collect_keeps_namesakes(root);
    write_cases(root);
    backup_cases(root);
    midi_button_case(root);
    inbox_case(root);
    std::cout << "FILE_SAFETY_PASS: load asks, save never replaces unasked, no paste into itself, "
                 "rename case, fast A after a move, preset file names, collect namesakes, synced writes, save backups, midi export, preset inbox\n";
}
