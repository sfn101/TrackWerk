// Drumlab: manual sequencing and layout (specs/drumlab-manual.md). Portable, no assert: CI runs it
// on Linux and Windows. Usage: ptdrumlab-manual-check <scratch folder>
#include "ui/drumlab.h"
#include "ui/canvas.h"
#include "ui/theme.h"
#include "ui/app_state.h"
#include "ui/studio_help.h"
#include "audio-engine.h"
#include "songcore/host.h"
#include "songcore/project_io.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

using namespace pt::ui;
namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

// Eight Mutable synth sources: loadable on every platform without sample files.
songcore::Project project() {
    auto p = songcore::make_default_project();
    const int models[] = {21, 22, 23, 23, 57, 59, 19, 17};
    for (int lane = 0; lane < 8; ++lane) {
        auto& ins = p.instruments[lane];
        ins.name = drumlab_lane_name(lane);
        ins.sampleFilePath = "/@mutable/p" + std::to_string(models[lane]) + "-100-160-120.wav";
        ins.root = {0, 3};
    }
    return p;
}

int lane_hits(const DrumLabState& s, int lane) {
    int n = 0;
    for (int step = 0; step < s.steps; ++step) n += s.grid[lane][step].velocity > 0;
    return n;
}

struct Rig {
    std::unique_ptr<AudioEngine> engine = std::make_unique<AudioEngine>();
    std::unique_ptr<songcore::SongcoreHost> host;
    Rig() {
        engine->setDeviceSampleRate(48000);
        host = std::make_unique<songcore::SongcoreHost>(engine.get(), 48000);
        host->push_project(songcore::serialize_project(project()));
        host->load_media("");
        host->push_params();
    }
    bool press(DrumLabState& s, Button b, ButtonMods m = {}) {
        return drumlab_button({b, ButtonAction::PRESSED, m}, s, *host, *engine);
    }
    bool release(DrumLabState& s, Button b, ButtonMods m = {}) {
        return drumlab_button({b, ButtonAction::RELEASED, m}, s, *host, *engine);
    }
    void tap_a(DrumLabState& s) { press(s, Button::A); release(s, Button::A); }
    void a_arrow(DrumLabState& s, Button arrow) {
        ButtonMods a; a.a = true;
        press(s, Button::A); press(s, arrow, a); release(s, Button::A);
    }
    void run(DrumLabState& s, int blocks) {
        float block[1024];
        for (int i = 0; i < blocks; ++i) { drumlab_tick(s, *host, *engine); engine->processLiveBlock(block, 512, 2, 48000); }
    }
};
}  // namespace

int main(int argc, char** argv) {
    Rig rig;
    ButtonMods l; l.l = true;

    // CLEAR on a fresh Drumlab: no question, an empty grid that Step Edit can use at once.
    {
        DrumLabState s;
        check(drumlab_clear(s) && !s.confirmClear, "CLEAR on a fresh grid does not ask");
        check(s.generated && !s.dirty && drumlab_hit_count(s) == 0, "the cleared grid is current and empty");
        DrumLabState fresh; fresh.row = 9; rig.tap_a(fresh);
        check(fresh.gridFocused, "A on SLOT opens Step Edit without Generate");
    }

    // CLEAR keeps locked lanes and asks only when unapplied hits would go.
    {
        DrumLabState s; s.activeLanes = 8;
        drumlab_generate(s);
        s.lanes[2].locked = true;
        const auto kept = s.grid[2];
        const auto before = s.grid;
        check(!drumlab_clear(s) && s.confirmClear && s.grid == before, "unapplied hits: CLEAR asks and changes nothing");
        s.row = 18;
        rig.press(s, Button::B);
        check(!s.confirmClear && s.grid == before, "B keeps the pattern");
        rig.tap_a(s);
        check(s.confirmClear && s.grid == before, "A on CLEAR asks first");
        rig.tap_a(s);
        check(!s.confirmClear && s.grid[2] == kept && lane_hits(s, 2) > 0, "A again clears, the locked lane keeps its hits");
        bool othersEmpty = true;
        for (int lane = 0; lane < 8; ++lane) if (lane != 2) othersEmpty &= lane_hits(s, lane) == 0;
        check(othersEmpty, "every unlocked lane is empty");

        DrumLabState applied; applied.activeLanes = 5;
        drumlab_generate(applied);
        songcore::SongcoreHost host(nullptr, 48000);
        host.push_project(songcore::serialize_project(project()));
        check(drumlab_apply(applied, host), "apply a generated pattern");
        check(drumlab_clear(applied) && !applied.confirmClear, "after Apply, CLEAR does not ask");
    }

    // Empty lanes and a single lane: Generate, Apply and Preview skip what has no sound.
    {
        DrumLabState s; s.activeLanes = 3;
        s.lanes[1].instrument = DRUMLAB_NO_SOURCE;
        check(drumlab_generate(s) && lane_hits(s, 1) == 0 && lane_hits(s, 0) > 0, "Generate leaves a -- lane empty");
        s.grid[2] = {};   // a lane with a sound but no hits
        songcore::SongcoreHost host(nullptr, 48000);
        host.push_project(songcore::serialize_project(project()));
        const int chainsBefore = [&] { int n = 0; for (const auto& t : host.project().tracks) for (int c : t.chainRefs) n += c >= 0; return n; }();
        check(drumlab_apply(s, host), "Apply with an empty lane");
        const int row = s.appliedSongRow;
        const auto& tracks = host.project().tracks;
        check(tracks[0].chainRefs[row] >= 0, "the lane with sound and hits gets a chain");
        check(tracks[1].chainRefs[row] == -1 && tracks[2].chainRefs[row] == -1, "silent and hitless lanes write no chain");
        int chainsAfter = 0; for (const auto& t : tracks) for (int c : t.chainRefs) chainsAfter += c >= 0;
        check(chainsAfter == chainsBefore + 1, "exactly one chain claimed");

        DrumLabState one; one.activeLanes = 1;
        check(drumlab_generate(one) && drumlab_preview(one, *rig.host, *rig.engine), "a single active lane generates and previews");
        rig.run(one, 20);
        drumlab_stop(one, *rig.host, *rig.engine);

        DrumLabState none; none.activeLanes = 2;
        none.lanes[0].instrument = none.lanes[1].instrument = DRUMLAB_NO_SOURCE;
        drumlab_clear(none);
        check(!drumlab_preview(none, *rig.host, *rig.engine), "Preview refuses when no lane has a sound");

        DrumLabState lanes; lanes.row = 1; lanes.activeLanes = 2;
        rig.a_arrow(lanes, Button::DPAD_LEFT); rig.a_arrow(lanes, Button::DPAD_LEFT);
        check(lanes.activeLanes == 1, "Sources goes down to 1 lane");
        DrumLabState slot; slot.row = 9; slot.col = 0; slot.lanes[0].instrument = 0;
        rig.a_arrow(slot, Button::DPAD_LEFT);
        check(slot.lanes[0].instrument == DRUMLAB_NO_SOURCE, "SLOT left of 00 is --");
    }

    // Step Edit keys.
    {
        DrumLabState s; s.activeLanes = 4;
        drumlab_clear(s);
        s.row = 9; s.col = 0; rig.tap_a(s);
        check(s.gridFocused, "Step Edit open");
        rig.tap_a(s);
        const int v = s.grid[0][0].velocity;
        check(v > 0, "A toggles a hit on");
        rig.a_arrow(s, Button::DPAD_UP);
        check(s.grid[0][0].velocity == std::min(127, v + 8), "A+Up raises the velocity");
        rig.a_arrow(s, Button::DPAD_DOWN); rig.a_arrow(s, Button::DPAD_DOWN);
        check(s.grid[0][0].velocity == v - 8, "A+Down lowers it");
        const int d = s.grid[0][0].delayTicks;
        rig.a_arrow(s, Button::DPAD_RIGHT);
        check(s.grid[0][0].delayTicks == d + 1 && s.gridStep == 0, "A+Right nudges later, the cursor stays");
        rig.a_arrow(s, Button::DPAD_LEFT);
        check(s.grid[0][0].delayTicks == d, "A+Left nudges back");
        for (int i = 0; i < 20; ++i) rig.a_arrow(s, Button::DPAD_RIGHT);
        check(s.grid[0][0].delayTicks == 11, "the nudge stops at 11 ticks");
        rig.tap_a(s);
        check(s.grid[0][0].velocity == 0, "A toggles it off");
        rig.tap_a(s);

        rig.press(s, Button::DPAD_RIGHT, l);
        check(s.gridFocused && s.gridStep == 0 && s.row == 9, "L+Right is ignored in Step Edit");
        rig.press(s, Button::DPAD_DOWN, l);
        check(s.previewRunning, "L+Down starts the preview loop");
        rig.release(s, Button::DPAD_DOWN, l);
        rig.run(s, 10);
        rig.press(s, Button::DPAD_DOWN, l);
        check(!s.previewRunning, "L+Down again stops it");
        drumlab_stop(s, *rig.host, *rig.engine);
    }

    // Pages: L+Left/Right switches between GROOVE and SOURCES, back on the last row used.
    {
        DrumLabState s; s.row = 5;
        check(!drumlab_sources_page(s), "starts on GROOVE");
        rig.press(s, Button::DPAD_RIGHT, l);
        check(drumlab_sources_page(s) && s.row == 9, "L+Right opens SOURCES");
        rig.press(s, Button::DPAD_DOWN); rig.press(s, Button::DPAD_DOWN);
        rig.press(s, Button::DPAD_LEFT, l);
        check(!drumlab_sources_page(s) && s.row == 5, "L+Left returns to GROOVE on its row");
        rig.press(s, Button::DPAD_RIGHT, l);
        check(s.row == 11, "and SOURCES remembers its row too");
        s.row = 16; rig.press(s, Button::DPAD_DOWN);
        check(s.row >= 17 && drumlab_sources_page(s), "the buttons sit under SOURCES");
        rig.press(s, Button::DPAD_LEFT, l);
        check(s.row >= 17 && !drumlab_sources_page(s), "and switch to GROOVE without leaving them");
        rig.press(s, Button::DPAD_UP);
        check(s.row == 8, "Up from the buttons returns to GROOVE's last row");
        s.row = 0; rig.press(s, Button::DPAD_UP);
        check(s.row == 0, "Up stops at the page's first row");
        s.row = 9; rig.press(s, Button::DPAD_UP);
        check(s.row == 9, "Up does not cross from SOURCES into GROOVE");
    }

    // L+A on a lane (drumlab-load-sound.md): open its instrument, with the browser when it is empty.
    {
        DrumLabState s; s.row = 10;   // SNR, instrument 01, loaded
        const auto la = [&] { rig.press(s, Button::A, l); rig.release(s, Button::A); };
        la();
        check(s.openInstrument == 1 && !s.openBrowser && !s.gridFocused, "a loaded lane asks for INSTRUMENT 01, no browser");
        s.openInstrument = -1; s.lanes[1].instrument = 40;   // nothing loaded in 40
        la();
        check(s.openInstrument == 40 && s.openBrowser, "an empty instrument asks for the browser on it");
        s.openInstrument = -1; s.openBrowser = false;
        s.lanes[1].instrument = DRUMLAB_NO_SOURCE; s.lanes[3].instrument = 8;   // 8 is free but taken by OH
        la();
        check(s.lanes[1].instrument == 9 && s.openInstrument == 9 && s.openBrowser,
              "a -- lane takes the first free instrument no other lane uses");
        s.openInstrument = -1; s.openBrowser = false; s.row = 4;
        la();
        check(s.openInstrument == -1 && !s.gridFocused, "L+A on GROOVE does nothing");
    }

    // The Studio's Drumlab help fits its panels (quick help 8 lines, topic pages 10).
    {
        AppState app; app.studioTab = 5; Canvas c;
        check(studio_help_wrapped_lines(c, studio_help_quick_content(app)).size() <= 8, "Drumlab quick help fits");
        app.drumLab.gridFocused = true;
        check(studio_help_wrapped_lines(c, studio_help_quick_content(app)).size() <= 8, "Step Edit quick help fits");
        bool pagesFit = true;
        for (int page = 0; page < STUDIO_HELP_PAGES; ++page)
            pagesFit &= studio_help_wrapped_lines(c, studio_help_content(app, 5, page)).size() <= 10;
        check(pagesFit, "Drumlab help pages fit");
    }

    // Screens for a reviewer (PPM): both pages, Step Edit, the CLEAR question and help.
    if (argc > 1) {
        std::filesystem::create_directories(argv[1]);
        const auto shot = [&](const char* name, const DrumLabState& s) {
            Canvas c; const auto theme = theme_studio();
            c.surface_rect(0, 0, 640, 480, theme.background);
            drumlab_draw(c, s, theme, rig.host->project());
            std::ofstream f(std::filesystem::path(argv[1]) / name, std::ios::binary);
            f << "P6\n640 480\n255\n";
            for (int i = 0; i < 640 * 480; ++i) {
                const auto p = c.pixels()[i];
                const char rgb[] = {char(p >> 16), char(p >> 8), char(p)};
                f.write(rgb, 3);
            }
        };
        DrumLabState s; s.activeLanes = 6; s.lanes[5].instrument = DRUMLAB_NO_SOURCE; drumlab_generate(s);
        s.row = 4; shot("drumlab-groove.ppm", s);
        s.row = 14; s.col = 0; shot("drumlab-sources.ppm", s);
        s.row = 18; drumlab_clear(s); s.footerOnSources = true; shot("drumlab-clear-question.ppm", s);
        s.confirmClear = false; s.row = 9; s.gridFocused = true; shot("drumlab-step-edit.ppm", s);
        s.gridFocused = false; s.showHelp = true; shot("drumlab-help.ppm", s);
    }

    std::printf(failures ? "FAILED: %d\n" : "PASS: Drumlab manual sequencing and layout\n", failures);
    return failures ? 1 : 0;
}
