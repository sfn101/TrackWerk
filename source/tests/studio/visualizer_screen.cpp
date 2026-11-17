// ptvisualizer-check: the VI screen (specs/fullscreen-visualizer.md). R+LEFT on SONG reaches it and
// R+RIGHT leaves it, R+UP/DOWN stay; A+RIGHT/LEFT step the style with wrap-around, and the bar's
// setting follows; every other edit button does nothing. Each style is drawn at full size from
// synthetic sound, and saved as <run dir>/vi-<n>.ppm to look at. Builds with GCC and MSVC; no assert.
// usage: ptvisualizer-check <new run directory>
#include "audio-engine.h"
#include "songcore/host.h"
#include "ui/button_mapper.h"
#include "ui/input_dispatcher.h"
#include "ui/layout.h"
#include "ui/std_filesystem.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace pt::ui;

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

struct Rig {
    std::unique_ptr<AudioEngine> engine = std::make_unique<AudioEngine>();
    songcore::SongcoreHost host{engine.get(), 48000};
    AppState state;
    StdFileSystem fs;
    InputDispatcher dispatch;
    MapperState mapper;
    ButtonMods held;
    uint64_t now = 1000;
    explicit Rig(const std::string& dir) : fs(dir), dispatch(state, host, fs) {
        host.new_project();
        state.project = &host.edit_project();
        state.currentScreen = ScreenType::SONG;
        state.previousColumn = 0;
    }
    void send(Button b, ButtonAction a) {
        now += 400;   // a human pause: two A presses within 300 ms are the A,A double-tap
        dispatch.set_now(static_cast<long long>(now));
        handle_button(ButtonEvent{b, a, held}, dispatch, mapper, now);
    }
    void tap(Button b) { send(b, ButtonAction::PRESSED); send(b, ButtonAction::RELEASED); }
    void chord(Button mod, Button b) {
        (mod == Button::R_SHIFT ? held.r : mod == Button::A ? held.a : mod == Button::B ? held.b : held.l) = true;
        send(mod, ButtonAction::PRESSED);
        tap(b);
        send(mod, ButtonAction::RELEASED);
        held = ButtonMods{};
    }
};

void save_ppm(const std::filesystem::path& path, const Canvas& c) {
    std::ofstream out(path, std::ios::binary);
    out << "P6\n" << DESIGN_W << " " << DESIGN_H << "\n255\n";
    for (int i = 0; i < DESIGN_W * DESIGN_H; ++i) {
        const auto p = c.pixels()[i];
        const char rgb[] = {char(p >> 16), char(p >> 8), char(p)};
        out.write(rgb, 3);
    }
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <new run directory>\n", argv[0]);
        return 2;
    }
    const std::filesystem::path dir = std::filesystem::path(argv[1]);
    std::filesystem::create_directories(dir / "data");
    Rig r((dir / "data").string());

    // ── getting there and back ──
    r.chord(Button::R_SHIFT, Button::DPAD_LEFT);
    check(r.state.currentScreen == ScreenType::VISUALIZER, "R+LEFT on SONG goes to VI");
    r.chord(Button::R_SHIFT, Button::DPAD_UP);
    r.chord(Button::R_SHIFT, Button::DPAD_DOWN);
    r.chord(Button::R_SHIFT, Button::DPAD_LEFT);
    check(r.state.currentScreen == ScreenType::VISUALIZER, "R+UP, R+DOWN and R+LEFT stay on VI");
    r.chord(Button::R_SHIFT, Button::DPAD_RIGHT);
    check(r.state.currentScreen == ScreenType::SONG, "R+RIGHT goes back to SONG");
    r.chord(Button::R_SHIFT, Button::DPAD_LEFT);

    // ── the style ──
    const int count = static_cast<int>(SettingsModule::visualizer_names().size());
    check(count == 9, "nine styles");
    r.state.theme.visualizerType = VisualizerType::SCOPE;
    r.chord(Button::A, Button::DPAD_RIGHT);
    check(r.state.theme.visualizerType == VisualizerType::FLAT, "A+RIGHT: next style (the setting the bar uses)");
    check(r.state.vizNameUntilMs > r.now, "...and its name is shown");
    r.chord(Button::A, Button::DPAD_LEFT);
    r.chord(Button::A, Button::DPAD_LEFT);
    check(r.state.theme.visualizerType == VisualizerType::PLASMA, "A+LEFT from the first wraps to the last");
    r.chord(Button::A, Button::DPAD_RIGHT);
    check(r.state.theme.visualizerType == VisualizerType::SCOPE, "A+RIGHT from the last wraps to the first");

    // ── no accidental edits ──
    const std::string before = songcore::serialize_project(r.host.project());
    for (Button b : {Button::DPAD_UP, Button::DPAD_DOWN, Button::DPAD_LEFT, Button::DPAD_RIGHT, Button::A, Button::B})
        r.tap(b);
    r.chord(Button::A, Button::DPAD_UP);
    r.chord(Button::A, Button::DPAD_DOWN);
    r.chord(Button::B, Button::DPAD_LEFT);
    r.chord(Button::L_SHIFT, Button::DPAD_RIGHT);
    check(r.state.currentScreen == ScreenType::VISUALIZER &&
          r.state.theme.visualizerType == VisualizerType::SCOPE &&
          songcore::serialize_project(r.host.project()) == before && !r.state.project_dirty(),
          "other buttons change nothing");

    // ── every style at full size, from a synthetic chord ──
    std::vector<float> wave(WAVEFORM_SIZE), tracks(static_cast<size_t>(TRACK_WAVEFORM_COUNT) * WAVEFORM_SIZE);
    std::vector<float> spectrum(OscilloscopeModule::NUM_BARS);
    for (int i = 0; i < WAVEFORM_SIZE; ++i) {
        wave[static_cast<size_t>(i)] = 0.25f * std::sin(i * 0.05f) + 0.08f * std::sin(i * 0.31f);
        for (int t = 0; t < TRACK_WAVEFORM_COUNT; ++t)
            tracks[static_cast<size_t>(t) * WAVEFORM_SIZE + i] = 0.2f * std::sin(i * 0.02f * (t + 1));
    }
    for (int b = 0; b < OscilloscopeModule::NUM_BARS; ++b)
        spectrum[static_cast<size_t>(b)] = 0.9f * std::exp(-b / 14.f) * (0.6f + 0.4f * std::sin(b * 0.7f));
    r.state.waveform = wave.data();
    r.state.trackWaveforms = tracks.data();
    r.state.spectrum = spectrum.data();
    r.state.trackMask = 0xFF;
    r.state.isPlaying = true;
    r.state.settings.sceneMotion = true;
    r.state.vizNameUntilMs = 0;

    TrackerLayout layout;
    for (int style = 0; style < count; ++style) {
        r.state.theme.visualizerType = static_cast<VisualizerType>(style);
        r.state.animationMs = 5000 + static_cast<uint64_t>(style) * 37;
        Canvas c;
        layout.draw(c, r.state);
        // Something besides the background was drawn inside the main area.
        int lit = 0;
        const auto bg = c.pixels()[(DESIGN_H - 20) * DESIGN_W + 20];
        for (int y = EDITOR_Y + 20; y < DESIGN_H - 20; y += 3)
            for (int x = SIDE_SPACER + 10; x < EDITOR_CLIP_RIGHT - 10; x += 3)
                lit += c.pixels()[y * DESIGN_W + x] != bg;
        // FLAT is the quiet style by design: one line along the foot.
        if (static_cast<VisualizerType>(style) != VisualizerType::FLAT)
            check(lit > 50, SettingsModule::visualizer_names()[static_cast<size_t>(style)] + " draws across the main area");
        save_ppm(dir / ("vi-" + std::to_string(style) + ".ppm"), c);
    }

    std::printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
