// The key bar (specs/help-and-lessons.md, phase B2): with the bar on, every screen moves up by its height
// (taken from the top strip), so nothing of a screen is hidden under it, in 4:3 and 16:9. Portable, no assert:
// CI runs it on Linux and Windows. Usage: ptkey-bar-check
#include "songcore/project_io.h"
#include "ui/key_bar.h"
#include "ui/layout.h"
#include "ui/theme.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>

using namespace pt::ui;

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::printf("%s: %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}
}  // namespace

int main() {
    const ScreenType screens[] = {ScreenType::SONG,    ScreenType::CHAIN,   ScreenType::PHRASE,
                                  ScreenType::INSTRUMENT, ScreenType::TABLE, ScreenType::PROJECT,
                                  ScreenType::GROOVE,  ScreenType::SCALE,   ScreenType::MODS,
                                  ScreenType::INST_POOL, ScreenType::MIXER, ScreenType::EFFECTS,
                                  ScreenType::SETTINGS, ScreenType::MIDI,   ScreenType::MIDI_MAP,
                                  ScreenType::PERFORM, ScreenType::VISUALIZER};
    for (bool wide : {false, true}) {
        set_design_width(wide);
        auto project = songcore::make_default_project();
        AppState s;
        s.project = &project;
        s.theme = theme_classic();
        TrackerLayout layout;
        auto c = std::make_unique<Canvas>();
        auto off = std::make_unique<Canvas>();
        for (ScreenType screen : screens) {
            s.currentScreen = screen;
            s.settings.keyBar = false;
            layout.draw(*off, s);
            s.settings.keyBar = true;
            layout.draw(*c, s);
            // The bar takes its height from the top strip: the screen's bottom rows, which the bar now
            // covers, must reappear unchanged right above it.
            // The background's texture is drawn by screen position, so it changes when moved; text and
            // lines are compared, as colours far apart.
            const auto far = [](uint32_t a, uint32_t b) {
                int most = 0;
                for (int sh : {0, 8, 16}) {
                    const int d = static_cast<int>((a >> sh) & 0xFF) - static_cast<int>((b >> sh) & 0xFF);
                    most = std::max(most, d < 0 ? -d : d);
                }
                return most > 40;
            };
            // SETTINGS shows the KEY BAR row's own value, ON in one picture and OFF in the other: its
            // value column is left out there.
            const bool settings = screen == ScreenType::SETTINGS;
            int moved = 0;
            for (int y = DESIGN_H - KEY_BAR_H; y < DESIGN_H; ++y)
                for (int x = 0; x < (settings ? 180 : DESIGN_W); ++x)
                    moved += far(c->pixels()[static_cast<size_t>(y - KEY_BAR_H) * DESIGN_W + x],
                                 off->pixels()[static_cast<size_t>(y) * DESIGN_W + x]);
            const std::string name = std::string(wide ? "16:9 " : "4:3 ") + screen_label(screen);
            check(moved == 0, name + ": the rows under the bar move up above it" +
                                  (moved ? " (" + std::to_string(moved) + " pixels differ)" : ""));
        }
    }
    std::printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
