// SPDX-License-Identifier: GPL-3.0-or-later
// ptstudio-xy-check: the XY pad page of Studio > Synth. It is the last page of every synth model (R+Left/Right
// reaches it), shows the first two macros as X and Y, and while L (the audition) is held the D-pad moves the dot
// instead of the cursor. Without L the D-pad navigates as before; A + arrows edits the two cards as on any page.
#include "audio-engine.h"
#include "songcore/host.h"
#include "songcore/project_io.h"
#include "studio/synth.h"
#include "ui/layout.h"
#include "ui/std_filesystem.h"
#include "ui/studio.h"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <cstdlib>
#include <fstream>
#include <memory>

using namespace pt::ui;
namespace fs = std::filesystem;

namespace {

struct Rig {
    std::unique_ptr<AudioEngine> engine = std::make_unique<AudioEngine>();
    songcore::SongcoreHost host{engine.get(), 48000};
    AppState s;
    StdFileSystem sfs;
    explicit Rig(const fs::path& dir) : sfs((dir / "data").string()) {
        assert(host.push_project(songcore::serialize_project(songcore::make_default_project())));
        s.project = &host.edit_project();
        s.studioOpen = true;
        s.studioTab = 0;
        s.currentInstrument = 2;
        s.studioSynthInstrument = 2;   // already bound: no re-open that would reset the page
        s.studioModel = studio::kDubSirenModel;
        s.studioHarmonics = 100;
        s.studioTimbre = 150;
        s.studioMorph = 128;
    }
    bool send(Button b, bool down, ButtonMods m) { return studio_button(ButtonEvent{b, down ? ButtonAction::PRESSED : ButtonAction::RELEASED, m}, s, host, *engine, sfs); }
    void tap(Button b, ButtonMods m = {}) { send(b, true, m); send(b, false, m); }
};

}  // namespace

int main(int argc, char** argv) {
    assert(argc == 2);
    const fs::path dir = fs::absolute(argv[1]);
    fs::create_directories(dir);

    // Every model has the page, as the last one, with two controls named after its first two macros.
    for (int model : {0, 24, 78, 102, studio::kDubSirenModel}) {
        Rig r(dir / ("m" + std::to_string(model)));
        r.s.studioModel = model;
        const int pages = studio_synth_page_count(r.s);
        r.s.studioSynthPage = pages - 1;
        assert(studio_synth_xy_page(r.s) && studio_synth_page_name(r.s) == "XY pad" && studio_synth_control_count(r.s) == 2);
        assert(studio_synth_control_label(r.s, 0) == std::string("X  ") + studio::macro_name(model, 0));
        assert(studio_synth_control_label(r.s, 1) == std::string("Y  ") + studio::macro_name(model, 1));
        r.s.studioSynthPage = pages - 2;
        assert(!studio_synth_xy_page(r.s));   // the page before it (ADSR) is not the pad
        assert(studio_synth_page_name(r.s) == "ADSR envelope");
    }

    Rig r(dir / "dub");
    r.s.studioSynthPage = studio_synth_page_count(r.s) - 1;
    r.s.studioRow = 3;
    ButtonMods none, held;
    held.l = true;
    assert(studio_synth_control_level(r.s, 0) == 100 && studio_synth_control_level(r.s, 1) == 150);

    // L held: the D-pad moves the dot (4 per press); up raises Y; the cursor stays where it was.
    r.tap(Button::DPAD_RIGHT, held);
    assert(r.s.studioHarmonics == 104 && r.s.studioTimbre == 150 && r.s.studioRow == 3);
    r.tap(Button::DPAD_UP, held);
    assert(r.s.studioHarmonics == 104 && r.s.studioTimbre == 154 && r.s.studioRow == 3);
    r.tap(Button::DPAD_DOWN, held);
    r.tap(Button::DPAD_DOWN, held);
    r.tap(Button::DPAD_LEFT, held);
    assert(r.s.studioHarmonics == 100 && r.s.studioTimbre == 146 && r.s.studioSynthCustom);
    assert(studio_synth_control_level(r.s, 0) == 100 && studio_synth_control_value(r.s, 1) == "57 %");

    // The edges hold.
    for (int i = 0; i < 80; ++i) { r.tap(Button::DPAD_RIGHT, held); r.tap(Button::DPAD_UP, held); }
    assert(r.s.studioHarmonics == 255 && r.s.studioTimbre == 255);
    for (int i = 0; i < 80; ++i) { r.tap(Button::DPAD_LEFT, held); r.tap(Button::DPAD_DOWN, held); }
    assert(r.s.studioHarmonics == 0 && r.s.studioTimbre == 0);

    // Without L the D-pad navigates (the dot stays), and A + arrows edits the two cards like any page.
    r.s.studioHarmonics = 100; r.s.studioTimbre = 100;
    r.tap(Button::DPAD_RIGHT, none);
    assert(r.s.studioHarmonics == 100 && r.s.studioRow == 4);   // the cursor moved from X to Y
    ButtonMods a; a.a = true;
    r.send(Button::A, true, a);
    r.send(Button::DPAD_RIGHT, true, a);
    r.send(Button::DPAD_RIGHT, false, a);
    r.send(Button::A, false, none);
    assert(r.s.studioTimbre == 101 && r.s.studioHarmonics == 100);   // row 4 is Y

    // A page where L + D-pad means what it always did: the oscillator page (no dot moves).
    r.s.studioSynthPage = 0; r.s.studioRow = 3;
    r.s.studioHarmonics = 100;
    r.tap(Button::DPAD_RIGHT, held);
    assert(r.s.studioHarmonics == 100);
    std::printf("STUDIO_XY_PASS\n");

    // The frog models show the stage and move the firefly on the FIRST page too.
    for (int model : {studio::kFrogVoiceModel, studio::kSingFrogModel}) {
        Rig f(dir / ("frog" + std::to_string(model)));
        f.s.studioModel = model;
        f.s.studioSynthPage = 0;
        assert(studio_synth_xy_page(f.s) && studio_synth_frog_model(f.s));
        f.s.studioRow = 3;
        f.tap(Button::DPAD_RIGHT, held);
        assert(f.s.studioHarmonics == 104 && f.s.studioRow == 3);
        f.s.studioSynthPage = 1;
        assert(!studio_synth_xy_page(f.s));   // the engine pages are plain
    }

    // FROGGY's lyric templates: A + Up / Down on the Lyrics row steps through Presets/Lyrics, starters included.
    {
        Rig f(dir / "froggy");
        f.s.studioModel = studio::kSingFrogModel;
        f.s.studioSynthPage = 1;
        assert(studio_synth_control_label(f.s, 2) == "Lyrics");
        studio_synth_edit(f.s, f.host, *f.engine, 5, +1, true, &f.sfs);
        const std::string first = f.s.studioDraft.lyric;
        assert(!first.empty() && f.s.studioMessage.rfind("Lyrics: ", 0) == 0);
        assert(fs::exists(dir / "froggy/data/Presets/Lyrics/Pond Song.txt"));
        studio_synth_edit(f.s, f.host, *f.engine, 5, +1, true, &f.sfs);
        assert(f.s.studioDraft.lyric != first);
        studio_synth_edit(f.s, f.host, *f.engine, 5, -1, true, &f.sfs);
        assert(f.s.studioDraft.lyric == first);
        // a template of our own is listed too (and a long one is cut to the limit)
        {
            std::ofstream out(dir / "froggy/data/Presets/Lyrics/AAA Mine.txt");
            out << std::string(300, 'a') << "\n";
        }
        studio_synth_edit(f.s, f.host, *f.engine, 5, +1, true, &f.sfs);
        studio_synth_edit(f.s, f.host, *f.engine, 5, -1, true, &f.sfs);
        studio_synth_edit(f.s, f.host, *f.engine, 5, -1, true, &f.sfs);
        assert(f.s.studioDraft.lyric.size() == size_t(studio::sing::kMaxTextChars) && f.s.studioDraft.lyric[0] == 'a');
        // without A + Up / Down the keyboard opens with the current words
        studio_synth_edit(f.s, f.host, *f.engine, 5, +1, false, &f.sfs);
        assert(f.s.qwerty.isOpen && f.s.qwerty.context == QwertyContext::LYRIC_EDIT);
        std::printf("lyric templates ok\n");
    }

    // Touch: a finger on the pad sets the dot and drags it; the sound follows; lifting it ends the drag.
    {
        Rig t(dir / "touch");
        t.s.studioSynthPage = studio_synth_page_count(t.s) - 1;
        assert(studio_synth_xy_page(t.s));
        assert(!studio_touch_canvas(t.s, t.host, *t.engine, 10, 10, 0));          // outside the pad: not ours
        assert(studio_touch_canvas(t.s, t.host, *t.engine, 398 + 8, 254 + 8, 0));  // top-left corner of the area: x 0, y max
        assert(t.s.studioHarmonics == 0 && t.s.studioTimbre == 255 && t.s.studioSynthCustom);
        assert(studio_touch_canvas(t.s, t.host, *t.engine, 398 + 8 + 201, 254 + 8 + 75, 1));   // the far corner
        assert(t.s.studioHarmonics == 255 && t.s.studioTimbre == 0);
        assert(studio_touch_canvas(t.s, t.host, *t.engine, 5, 5, 1) && t.s.studioHarmonics == 0 && t.s.studioTimbre == 255);   // a drag may leave the pad
        assert(studio_touch_canvas(t.s, t.host, *t.engine, 0, 0, 2) && !t.s.canvasTouchActive);
        assert(!studio_touch_canvas(t.s, t.host, *t.engine, 300, 300, 1));        // a move without a down on the pad
        // a second finger does not move the dot, and its lifting does not end the first finger's drag
        assert(studio_touch_canvas(t.s, t.host, *t.engine, 398 + 8, 254 + 8, 0, /*finger*/ 7));
        assert(!studio_touch_canvas(t.s, t.host, *t.engine, 398 + 8 + 100, 254 + 8 + 30, 1, /*finger*/ 9));
        assert(t.s.studioHarmonics == 0 && t.s.studioTimbre == 255);
        assert(!studio_touch_canvas(t.s, t.host, *t.engine, 0, 0, 2, /*finger*/ 9) && t.s.canvasTouchActive);
        assert(studio_touch_canvas(t.s, t.host, *t.engine, 398 + 8 + 201, 254 + 8 + 75, 1, 7) && t.s.studioHarmonics == 255);
        assert(studio_touch_canvas(t.s, t.host, *t.engine, 0, 0, 2, 7) && !t.s.canvasTouchActive);
        // the frog stage on the first page
        Rig f(dir / "touchfrog");
        f.s.studioModel = studio::kSingFrogModel;
        f.s.studioSynthPage = 0;
        assert(studio_touch_canvas(f.s, f.host, *f.engine, 304 + 10 + 146, 254 + 8 + 24, 0));
        assert(std::abs(f.s.studioHarmonics - 127) <= 2 && std::abs(f.s.studioTimbre - 127) <= 4);
        studio_touch_canvas(f.s, f.host, *f.engine, 0, 0, 2);
        // not on a page without the pad
        f.s.studioSynthPage = 1;
        assert(!studio_touch_canvas(f.s, f.host, *f.engine, 304 + 10, 254 + 8, 0));
        std::printf("touch pad ok\n");
    }

    // The lyric travels with the project and with the instrument preset, and a text with a multi-byte character survives.
    {
        Rig q(dir / "roundtrip");
        auto project = songcore::make_default_project();
        project.instruments[2].lyric = "la la ribbit \xC3\xBC";
        const std::string text = songcore::serialize_project(project);
        assert(text.find("\"lyric\":") != std::string::npos);
        assert(q.host.push_project(text));
        assert(q.host.project().instruments[2].lyric == "la la ribbit \xC3\xBC");
        songcore::InstrumentPreset preset;
        preset.instrument = project.instruments[2];
        assert(songcore::serialize_instrument_preset(preset).find("la la ribbit") != std::string::npos);
        std::printf("lyric roundtrip ok\n");
    }
}
