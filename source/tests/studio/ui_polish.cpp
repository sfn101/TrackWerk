// Visual polish on the tracker screens, checked as pixels and contrast:
//   the playing row's band (copper / phosphor / plain) appears only on that row and only while playing;
//   the scope's phosphor afterglow follows Scene looks and clears on silence or a classic theme;
//   the status line's error ink keeps 4.5:1 on every built-in Scene and the default theme.
#include "audio-engine.h"
#include "songcore/host.h"
#include "songcore/project_io.h"
#include "ui/color_space.h"
#include "ui/helpers.h"
#include "ui/layout.h"
#include "ui/modules/oscilloscope.h"
#include "ui/scene_layout.h"
#include "ui/scene_presets.h"
#include "ui/scene_picker.h"
#include "ui/studio.h"
#include "ui/std_filesystem.h"
#include "ui/settings_store.h"
#include "ui/navigation.h"
#include "ui/screen.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace pt::ui;

namespace {
int differing(const Canvas& a, const Canvas& b, int x, int y, int w, int h) {
    int n = 0;
    for (int yy = y; yy < y + h; ++yy)
        for (int xx = x; xx < x + w; ++xx) n += a.pixels()[yy * DESIGN_W + xx] != b.pixels()[yy * DESIGN_W + xx];
    return n;
}
}  // namespace

int main() {
    auto engine = std::make_unique<AudioEngine>();
    songcore::SongcoreHost host(engine.get(), 48000);
    auto p = songcore::make_default_project();
    p.tracks[0].chainRefs = {0};
    p.chains[0].phraseRefs[0] = 0;
    assert(host.push_project(songcore::serialize_project(p)));

    AppState s;
    s.project = &host.edit_project();
    s.currentScreen = ScreenType::PHRASE;
    s.currentPhrase = 0;
    TrackerLayout layout;

    // ── The playing row ─────────────────────────────────────────────────────────────────────────
    for (int scene : {-1, 0, 12, 20, 24}) {
        s.theme = scene < 0 ? theme_default() : scene_preset(scene, VisualizerType::FLAT);
        Canvas idle, playing;
        s.isPlaying = false; for (auto& h : s.playheads) h = {};
        layout.draw(idle, s);
        s.isPlaying = true; s.playheads[0].phraseId = 0; s.playheads[0].step = 6;
        layout.draw(playing, s);
        // Row 6 changes across most of its width (the band), rows 4..5 carry the fading trail of the
        // rows just played, rows 0..3 and 7..F do not change.
        const auto scene_layout_of = scene_layout(s.theme);
        const int editorX = scene_layout_of.railLeft ? 131 : 10, editorY = scene_layout_of.scopeBottom ? 6 : 82;
        const int rowY = editorY + ROW_HEIGHT + 14 + ROW_HEIGHT + 6 * ROW_HEIGHT;
        assert(differing(idle, playing, editorX, rowY, 499, ROW_HEIGHT) > 499 * ROW_HEIGHT / 3);
        assert(differing(idle, playing, editorX, rowY - 6 * ROW_HEIGHT, 499, 4 * ROW_HEIGHT) == 0);
        assert(differing(idle, playing, editorX, rowY - 2 * ROW_HEIGHT, 499, 2 * ROW_HEIGHT) > 0);
        assert(differing(idle, playing, editorX, rowY + ROW_HEIGHT, 499, 9 * ROW_HEIGHT) == 0);
    }
    s.isPlaying = false; for (auto& h : s.playheads) h = {};

    // ── Phosphor afterglow ──────────────────────────────────────────────────────────────────────
    std::array<float, WAVEFORM_SIZE> wave{};
    auto set_wave = [&](float phase) {
        for (int i = 0; i < WAVEFORM_SIZE; ++i) wave[size_t(i)] = float(std::sin(i * .05 + phase) * .2);
        s.waveform = wave.data();
    };
    auto scope_of = [&](const Theme& theme, float phase, int frames) {
        s.theme = theme; Canvas c;
        for (int f = 0; f < frames; ++f) { set_wave(phase + f * 1.3f); layout.draw(c, s); }
        return c;
    };
    const Theme scene = scene_preset(12, VisualizerType::SCOPE);   // Terminal: scope at the bottom
    const int scopeY = DESIGN_H - SCREEN_SPACER - OscilloscopeModule::HEIGHT;
    {
        // Three frames on a Scene: the previous traces add glow pixels beside the live trace.
        TrackerLayout fresh;
        Canvas single; s.theme = scene; set_wave(2 * 1.3f); fresh.draw(single, s);
        const Canvas trail = scope_of(scene, 0.f, 3);
        assert(differing(single, trail, 10, scopeY, 620, 70) > 200);
        // Silence clears it: the next drawn trace stands alone again.
        s.waveform = nullptr; layout.draw(single, s);
        TrackerLayout clean; Canvas a, b;
        set_wave(.4f); layout.draw(a, s); clean.draw(b, s);
        assert(differing(a, b, 10, scopeY, 620, 70) == 0);
    }
    {
        // A classic theme never keeps a trail.
        Theme classic = theme_default(); classic.visualizerType = VisualizerType::SCOPE;
        TrackerLayout fresh; Canvas single; s.theme = classic; set_wave(2 * 1.3f); fresh.draw(single, s);
        const Canvas many = scope_of(classic, 0.f, 3);
        const auto sl = scene_layout(classic);
        const int y = sl.scopeBottom ? scopeY : SCREEN_SPACER;
        assert(differing(single, many, 10, y, 620, 70) == 0);
    }
    s.waveform = nullptr;

    // ── Scene animations ────────────────────────────────────────────────────────────────────────
    // RASTER bars and the CRACK INTRO scroller move in the scope strip, the VECTOR cube in the rail —
    // only while playing. Stopped, the clock changes nothing on screen.
    for (int scene : {0, 4, 8, 12, 16, 20, 24, 28}) {
        s.theme = scene_preset(scene, VisualizerType::SCOPE);
        const auto sl = scene_layout(s.theme);
        const int stripY = sl.scopeBottom ? scopeY : SCREEN_SPACER;
        const int railX = sl.railLeft ? SIDE_SPACER : right_bar_x(), railY = sl.scopeBottom ? SCREEN_SPACER : EDITOR_Y;
        const bool inRail = scene != 20 && scene != 24;   // Raster and Crack intro move in the scope strip
        auto frame = [&](bool playing, uint64_t ms) {
            TrackerLayout l; Canvas c;
            s.isPlaying = playing; for (auto& h : s.playheads) h = {};
            if (playing) { s.playheads[0].phraseId = 99; s.playheads[0].step = int(ms / 100) % 16; }   // off-screen phrase: no row band
            s.waveform = nullptr; s.animationMs = ms;
            l.draw(c, s);
            return c;
        };
        // Over half a second something must move (a beat-locked blink can be off at any two instants).
        const Canvas a = frame(true, 10000);
        int moved = 0;
        for (uint64_t later = 10090; later <= 10540; later += 90) {
            const Canvas b = frame(true, later);
            moved = std::max(moved, inRail ? differing(a, b, railX, railY, 115, DESIGN_H - EDITOR_Y - SCREEN_SPACER)
                                           : differing(a, b, 10, stripY, 620, 70));
        }
        if (moved <= 40) std::printf("scene %d moved %d\n", scene, moved);
        assert(moved > 40);
        const Canvas b = frame(true, 10180);
        const Canvas idleA = frame(false, 10000), idleB = frame(false, 10180);
        assert(differing(idleA, idleB, 0, 0, DESIGN_W, DESIGN_H) == 0);
        // The editor itself never moves with the clock.
        const int editorX = sl.railLeft ? 131 : 10, editorY = sl.scopeBottom ? SCREEN_SPACER : EDITOR_Y;
        assert(differing(a, b, editorX, editorY, 499, 16 * ROW_HEIGHT) == 0);
    }
    s.isPlaying = false;

    // ── Studio heading ──────────────────────────────────────────────────────────────────────────
    // On every Scene the Studio heading carries the family's strip while playing, and only then.
    for (int scene = 0; scene < kSceneCount; scene += 4) {
        s.theme = scene_preset(scene, VisualizerType::SCOPE);
        s.studioOpen = true; s.studioTab = 3;
        auto frame = [&](bool playing, uint64_t ms) {
            TrackerLayout l; Canvas c; s.isPlaying = playing; for (auto& h : s.playheads) h = {};
            if (playing) { s.playheads[0].phraseId = 99; s.playheads[0].step = int(ms / 100) % 16; }
            s.animationMs = ms; l.draw(c, s); return c;
        };
        const Canvas a = frame(true, 10000);
        int moved = 0;
        for (uint64_t later = 10090; later <= 10540; later += 90)
            moved = std::max(moved, differing(a, frame(true, later), 0, 0, DESIGN_W, 110));
        assert(moved > 20);
        assert(differing(frame(false, 10000), frame(false, 10540), 0, 0, DESIGN_W, DESIGN_H) == 0);
    }
    s.studioOpen = false; s.isPlaying = false; for (auto& h : s.playheads) h = {};

    // ── Scene browser live preview ──────────────────────────────────────────────────────────────
    // Each look's preview plays: its playing row walks the four steps and its family strip moves;
    // the family/preset lists beside it stay still.
    for (int scene = 0; scene < kSceneCount; scene += 4) {
        Canvas a, b;
        draw_scene_picker(a, scene, theme_default(), 40000);
        draw_scene_picker(b, scene, theme_default(), 40520);
        assert(differing(a, b, 325, 155, 299, 237) > 100);
        assert(differing(a, b, 16, 75, 300, 400) == 0);
    }

    // ── Appearance > Motion ─────────────────────────────────────────────────────────────────────
    {
        // Saved only when OFF: a default settings.json stays byte-identical; OFF survives a reload.
        const auto dir = std::filesystem::temp_directory_path() / "trackwerk-ui-polish-motion";
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        StdFileSystem fs((dir / "data").string());
        SettingsValues v; const Theme th = scene_preset(12, VisualizerType::SCOPE);
        assert(save_settings(fs, v, th));
        std::string blob; assert(fs.read_file(fs.settings_path(), blob) && blob.find("sceneMotion") == std::string::npos);
        v.sceneMotion = false; assert(save_settings(fs, v, th));
        SettingsValues back; Theme tb; assert(load_settings(fs, back, tb) && !back.sceneMotion);
        back.sceneMotion = true; assert(save_settings(fs, back, tb));
        SettingsValues again; assert(load_settings(fs, again, tb) && again.sceneMotion);
        std::filesystem::remove_all(dir);

        // The Appearance row toggles it through the real Studio input (A + Right on row 3).
        AppState a; a.project = &host.edit_project(); a.theme = th;
        open_studio_appearance(a); a.studioRow = 3;
        StdFileSystem nofs((std::filesystem::temp_directory_path() / "trackwerk-ui-polish-none").string());
        auto press = [&](Button b, ButtonAction act, ButtonMods m = {}) { studio_button({b, act, m}, a, host, *engine, nofs); };
        ButtonMods held; held.a = true;
        assert(a.settings.sceneMotion);
        // Codex round 2: a held D-pad auto-repeats PRESSED. Right means OFF and stays OFF through any
        // number of repeats; Left means ON likewise.
        for (const auto& [button, expected] : {std::pair{Button::DPAD_RIGHT, false}, std::pair{Button::DPAD_LEFT, true},
                                               std::pair{Button::DPAD_LEFT, true}, std::pair{Button::DPAD_RIGHT, false}}) {
            press(Button::A, ButtonAction::PRESSED);
            for (int repeat = 0; repeat < 4; ++repeat) {
                press(button, ButtonAction::PRESSED, held);
                assert(a.settings.sceneMotion == expected);
            }
            press(button, ButtonAction::RELEASED, held); press(Button::A, ButtonAction::RELEASED);
            assert(a.settings.sceneMotion == expected);
        }

        // OFF: a playing tracker on every Scene is still over time (the playing row stays marked).
        s.settings.sceneMotion = false;
        for (int scene = 0; scene < kSceneCount; scene += 4) {
            s.theme = scene_preset(scene, VisualizerType::SCOPE);
            auto frame = [&](uint64_t ms) {
                TrackerLayout l; Canvas c; s.isPlaying = true; for (auto& h : s.playheads) h = {};
                s.playheads[0].phraseId = 0; s.playheads[0].step = 5; s.waveform = nullptr; s.animationMs = ms;
                l.draw(c, s); return c;
            };
            assert(differing(frame(10000), frame(10540), 0, 0, DESIGN_W, DESIGN_H) == 0);
            Canvas p1, p2; draw_scene_picker(p1, scene, theme_default(), 0); draw_scene_picker(p2, scene, theme_default(), 0);
            assert(differing(p1, p2, 0, 0, DESIGN_W, DESIGN_H) == 0);
        }
        s.settings.sceneMotion = true; s.isPlaying = false; for (auto& h : s.playheads) h = {};
    }

    // ── Navigation map ──────────────────────────────────────────────────────────────────────────
    // Every cell the map can show reads differently: no letter stands for two screens.
    for (int col = 0; col < NAV_COLUMNS; ++col) {
        std::vector<std::string> seen;
        for (int row = 0; row < NAV_ROWS; ++row) {
            const bool main = row == NAV_MAIN_ROW;
            for (int c2 = 0; c2 < NAV_COLUMNS; ++c2) {
                if (!(main || c2 == col) || !SCREEN_GRID[row][c2]) continue;
                const std::string label = screen_map_label(*SCREEN_GRID[row][c2], main);
                if (std::find(seen.begin(), seen.end(), label) == seen.end()) seen.push_back(label);
            }
        }
        std::vector<ScreenType> screens;
        for (int row = 0; row < NAV_ROWS; ++row)
            for (int c2 = 0; c2 < NAV_COLUMNS; ++c2)
                if ((row == NAV_MAIN_ROW || c2 == col) && SCREEN_GRID[row][c2] &&
                    std::find(screens.begin(), screens.end(), *SCREEN_GRID[row][c2]) == screens.end())
                    screens.push_back(*SCREEN_GRID[row][c2]);
        assert(seen.size() == screens.size());
    }
    assert(std::string(screen_map_label(ScreenType::INST_POOL, false)) == "PL");
    // The arrival banner shows while its time runs, fades, and leaves the map as it was.
    {
        s.theme = scene_preset(12, VisualizerType::FLAT); s.currentScreen = ScreenType::SCALE; s.previousColumn = 2;
        auto map = [&](uint64_t until, uint64_t now) {
            TrackerLayout l; Canvas c; s.navBannerUntilMs = until; s.animationMs = now; l.draw(c, s); return c;
        };
        const Canvas none = map(0, 10000), full = map(10900, 10000), fading = map(10900, 10800), over = map(10900, 10900);
        const int mx = SIDE_SPACER, my = SCREEN_SPACER + 10;   // Terminal: rail left, map at its top
        assert(differing(none, full, mx, my, 115, 21) > 200);
        assert(differing(full, fading, mx, my, 115, 21) > 0);
        assert(differing(none, over, 0, 0, DESIGN_W, DESIGN_H) == 0);
        s.navBannerUntilMs = 0; s.currentScreen = ScreenType::PHRASE;
    }

    // ── Phosphor follows the scope (Codex round 2) ──────────────────────────────────────────────
    // A live scope that moves (layout change top ↔ bottom) draws no afterglow outside its new strip:
    // after the move it equals a fresh scope; while it stays put the trail returns.
    {
        std::array<float, WAVEFORM_SIZE> w{}; w.fill(.2f);
        OscilloscopeState st; st.waveform = w.data();
        OscilloscopeModule live;
        Canvas a, b, fresh;
        for (Canvas* c : {&a, &b, &fresh}) c->fill_rect(0, 0, DESIGN_W, DESIGN_H, 0xff000000);
        st.theme = scene_preset(0, VisualizerType::SCOPE);  live.draw(a, 10, 10, st);    // top
        st.theme = scene_preset(12, VisualizerType::SCOPE); live.draw(b, 10, 399, st);   // bottom
        OscilloscopeModule{}.draw(fresh, 10, 399, st);
        assert(differing(b, fresh, 0, 0, DESIGN_W, DESIGN_H) == 0);
        Canvas back, freshTop;
        for (Canvas* c : {&back, &freshTop}) c->fill_rect(0, 0, DESIGN_W, DESIGN_H, 0xff000000);
        st.theme = scene_preset(0, VisualizerType::SCOPE); live.draw(back, 10, 10, st);   // and back up
        OscilloscopeModule{}.draw(freshTop, 10, 10, st);
        assert(differing(back, freshTop, 0, 0, DESIGN_W, DESIGN_H) == 0);
        // Staying put with a changing wave: the trail is back, inside the strip only.
        Canvas still; still.fill_rect(0, 0, DESIGN_W, DESIGN_H, 0xff000000);
        for (int f = 0; f < 3; ++f) { for (int i = 0; i < WAVEFORM_SIZE; ++i) w[size_t(i)] = float(std::sin(i * .05 + f) * .2); live.draw(still, 10, 10, st); }
        Canvas one; one.fill_rect(0, 0, DESIGN_W, DESIGN_H, 0xff000000); OscilloscopeModule{}.draw(one, 10, 10, st);
        assert(differing(still, one, 10, 10, OscilloscopeModule::WIDTH, OscilloscopeModule::HEIGHT) > 100);
        assert(differing(still, one, 0, 10 + OscilloscopeModule::HEIGHT, DESIGN_W, DESIGN_H - 10 - OscilloscopeModule::HEIGHT) == 0);
    }

    // ── Graticule: Scenes only, static, under the trace ─────────────────────────────────────────
    // A stopped Scene scope shows the divisions; a stopped OCTA strip its eight lanes at rest. Inside the strip only, identical on every frame.
    {
        OscilloscopeState st;   // stopped: no waveform
        auto strip = [&](const Theme& t) {
            Canvas c; c.fill_rect(0, 0, DESIGN_W, DESIGN_H, 0xff000000);
            st.theme = t; OscilloscopeModule{}.draw(c, 10, 10, st); return c;
        };
        auto plain = [&](const Theme& t) {
            Canvas c; c.fill_rect(0, 0, DESIGN_W, DESIGN_H, 0xff000000);
            c.fill_rect(10, 10, OscilloscopeModule::WIDTH, OscilloscopeModule::HEIGHT, t.vizBackground);
            c.fill_rect(10, 10 + OscilloscopeModule::HEIGHT / 2, OscilloscopeModule::WIDTH, 1, t.vizCenterLine);
            return c;
        };
        // A stopped OCTA strip (the default) shows its eight lanes at rest with track numbers:
        // inside the strip only, the same on every frame.
        const Theme classic = theme_default();
        assert(differing(strip(classic), plain(classic), 10, 10, OscilloscopeModule::WIDTH, OscilloscopeModule::HEIGHT) > 100);
        assert(differing(strip(classic), plain(classic), 0, 10 + OscilloscopeModule::HEIGHT, DESIGN_W, DESIGN_H - 10 - OscilloscopeModule::HEIGHT) == 0);
        assert(differing(strip(classic), strip(classic), 0, 0, DESIGN_W, DESIGN_H) == 0);
        for (int i : {0, 12, 24, 28}) {
            const Theme t = scene_preset(i, VisualizerType::SCOPE);
            const Canvas a = strip(t), b = strip(t), p = plain(t);
            assert(differing(a, p, 10, 10, OscilloscopeModule::WIDTH, OscilloscopeModule::HEIGHT) > 300);
            assert(differing(a, p, 0, 10 + OscilloscopeModule::HEIGHT, DESIGN_W, DESIGN_H - 10 - OscilloscopeModule::HEIGHT) == 0);
            assert(differing(a, b, 0, 0, DESIGN_W, DESIGN_H) == 0);
        }
    }

    // ── Design patches 0028–0033: the new views are deterministic and follow their values ───────
    // Synth page pips, mixer numbers/faders, groove timeline, scale keys, MOD shapes (RND and DRK
    // seeded), reverb/delay strips: the same state draws the same pixels, a changed value changes
    // the picture, and a classic theme keeps its REV/DEL and MOD label face.
    {
        AppState d;
        d.project = &host.edit_project();
        auto& proj = host.edit_project();
        auto& mods = proj.instruments[0].modSlots;
        mods[0].type = songcore::ModType::LFO; mods[0].oscShape = 8;   // RND
        mods[1].type = songcore::ModType::LFO; mods[1].oscShape = 9;   // DRK
        mods[2].type = songcore::ModType::ADSR; mods[3].type = songcore::ModType::AHD;
        proj.grooves[0].steps.assign(16, -1); proj.grooves[0].steps[0] = 16; proj.grooves[0].steps[1] = 8;
        for (const ScreenType screen : {ScreenType::MIXER, ScreenType::GROOVE, ScreenType::SCALE,
                                        ScreenType::MODS, ScreenType::EFFECTS}) {
            for (int scene : {-1, 12, 28}) {
                d.theme = scene < 0 ? theme_default() : scene_preset(scene, VisualizerType::SCOPE);
                d.currentScreen = screen;
                Canvas a, b;
                layout.draw(a, d); layout.draw(b, d);
                assert(differing(a, b, 0, 0, DESIGN_W, DESIGN_H) == 0);
            }
        }
        auto changes = [&](ScreenType screen, auto&& edit) {
            d.theme = scene_preset(28, VisualizerType::SCOPE);
            d.currentScreen = screen;
            Canvas before, after;
            layout.draw(before, d); edit(); layout.draw(after, d);
            return differing(before, after, 0, 0, DESIGN_W, DESIGN_H);
        };
        assert(changes(ScreenType::EFFECTS, [&] { proj.delayFeedback = 0xE0; proj.delayPong = true; }) > 30);
        assert(changes(ScreenType::EFFECTS, [&] { proj.reverbFeedback = 0x10; }) > 30);
        assert(changes(ScreenType::MODS, [&] { mods[2].sustain = 0x10; }) > 10);
        assert(changes(ScreenType::GROOVE, [&] { proj.grooves[0].steps[0] = 12; proj.grooves[0].steps[1] = 12; }) > 30);
        assert(changes(ScreenType::SCALE, [&] { proj.scales[0].enabled[1] = 0; }) > 100);
        assert(changes(ScreenType::MIXER, [&] { proj.tracks[3].volume = 0x40; }) > 10);
        // Classic themes draw REV/DEL and the MOD labels in the theme face, as before: the label
        // face would differ from it, so a classic MIXER/MODS frame must not contain the narrow face.
        // (Checked indirectly: switching sceneChrome on alone changes those frames.)
        for (const ScreenType screen : {ScreenType::MIXER, ScreenType::MODS}) {
            d.currentScreen = screen;
            Theme classic = theme_default(), chrome = classic;
            chrome.sceneChrome = 1;
            Canvas a, b;
            d.theme = classic; layout.draw(a, d);
            d.theme = chrome; layout.draw(b, d);
            assert(differing(a, b, 0, 0, DESIGN_W, DESIGN_H) > 0);
        }
        // Codex review 10 · TRIG plays as ADSR: SUS and REL each move its strip.
        mods[2].type = songcore::ModType::TRIG; mods[2].sustain = 0; mods[2].release = 0;
        assert(changes(ScreenType::MODS, [&] { mods[2].sustain = 0xFF; }) > 10);
        assert(changes(ScreenType::MODS, [&] { mods[2].release = 0xFF; }) > 10);
        // A loaded key outside 0..11 (a .ptp with "scaleKey":25) draws as its pitch class, no out-of-range read.
        for (int scene : {-1, 12, 28}) {
            d.theme = scene < 0 ? theme_default() : scene_preset(scene, VisualizerType::SCOPE);
            d.currentScreen = ScreenType::SCALE;
            proj.scales[0].enabled = {1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 0};
            Canvas k25, k1, kNeg;
            proj.scaleKey = 25; layout.draw(k25, d);
            proj.scaleKey = 1;  layout.draw(k1, d);
            proj.scaleKey = -11; layout.draw(kNeg, d);
            assert(differing(k25, k1, 0, 0, DESIGN_W, DESIGN_H) == 0 && differing(kNeg, k1, 0, 0, DESIGN_W, DESIGN_H) == 0);
            proj.scaleKey = 0;
        }
        // Codex review 10 · the track numbers keep a band of their own: a level at the top of the
        // scale (+6 dB) changes nothing in it, in classic and Scene looks.
        for (int scene : {-1, 0, 12, 24, 28}) {
            d.theme = scene < 0 ? theme_default() : scene_preset(scene, VisualizerType::SCOPE);
            d.currentScreen = ScreenType::MIXER;
            const auto lay = scene_layout(d.theme);
            const int ex = lay.railLeft ? 131 : 10, ey = lay.scopeBottom ? 6 : 82;
            Canvas quiet, loud;
            std::fill(std::begin(d.trackPeaks), std::end(d.trackPeaks), 0.f); ++d.peaksVersion;
            layout.draw(quiet, d);
            std::fill(std::begin(d.trackPeaks), std::end(d.trackPeaks), 2.f); ++d.peaksVersion;
            layout.draw(loud, d);
            assert(differing(quiet, loud, ex, ey + 140, 470, 40) > 100);   // the meters do move
            assert(differing(quiet, loud, ex, ey + 24, 470, 12) == 0);     // the number band does not
            std::fill(std::begin(d.trackPeaks), std::end(d.trackPeaks), 0.f); ++d.peaksVersion;
        }
        mods.assign(4, songcore::ModSlot{});
        proj.grooves[0].steps = songcore::default_groove_steps();
        proj.scales[0].enabled.assign(12, 1);
        const auto fresh = songcore::make_default_project();
        proj.delayFeedback = fresh.delayFeedback; proj.delayPong = fresh.delayPong;
        proj.reverbFeedback = fresh.reverbFeedback; proj.tracks[3].volume = fresh.tracks[3].volume;
    }

    // ── Error ink ───────────────────────────────────────────────────────────────────────────────
    for (int i = -1; i < kSceneCount; ++i) {
        const Theme t = i < 0 ? theme_default() : scene_preset(i, VisualizerType::SCOPE);
        assert(wcag_contrast(error_ink(t), t.vizBackground) >= 4.5);
        assert(((error_ink(t) >> 16) & 255) > ((error_ink(t) >> 8) & 255));   // still reads as red
    }

    std::puts("PASS: playing-row band on exactly the playing row in 5 looks, phosphor trail on Scenes only and cleared by silence, error ink >= 4.5:1 on 33 themes, Scene animations in all eight families only while playing, unambiguous map labels, timed arrival banner, Studio heading animation on all eight families, live Scene browser preview, Appearance > Motion saved, direction-bound under auto-repeat and honoured, phosphor stays in a moved scope, graticule on Scenes only, design views 0028-0033 deterministic and following their values, TRIG strip, scale key mod 12, mixer number band free of levels");
    return 0;
}
