// ptwide-mode-check: wide mode (specs/wide-mode.md). The 854×480 canvas, the right bar at the right
// edge, the PHRASE side view (the note each other track plays on the same step), and the SCREEN
// setting's round trip. Builds with GCC and MSVC; no assert.
// usage: ptwide-mode-check <new run directory>   (it also writes each drawn screen there as a .bmp)
#include "songcore/project_ops.h"
#include "ui/layout.h"
#include "ui/scene_layout.h"
#include "ui/scene_presets.h"
#include "ui/settings_store.h"
#include "ui/std_filesystem.h"

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

// A 24-bit BMP of the canvas, to look at.
void write_bmp(const Canvas& c, const std::filesystem::path& path) {
    const int w = DESIGN_W, h = DESIGN_H, row = (w * 3 + 3) & ~3;
    const uint32_t size = 54 + static_cast<uint32_t>(row) * h;
    unsigned char head[54] = {'B', 'M'};
    const auto put = [&](int at, uint32_t v) { for (int i = 0; i < 4; ++i) head[at + i] = (v >> (8 * i)) & 255; };
    put(2, size); put(10, 54); put(14, 40); put(18, w); put(22, h);
    head[26] = 1; head[28] = 24;
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(head), 54);
    std::string line(static_cast<size_t>(row), '\0');
    for (int y = h - 1; y >= 0; --y) {
        for (int x = 0; x < w; ++x) {
            const uint32_t p = c.pixels()[static_cast<size_t>(y) * w + x];
            line[static_cast<size_t>(x) * 3] = static_cast<char>(p & 255);
            line[static_cast<size_t>(x) * 3 + 1] = static_cast<char>((p >> 8) & 255);
            line[static_cast<size_t>(x) * 3 + 2] = static_cast<char>((p >> 16) & 255);
        }
        out.write(line.data(), row);
    }
}

// How many pixels of a rect differ between two canvases' rows.
int differing(const Canvas& c, int x, int y0, int y1, int w, int h) {
    int n = 0;
    for (int dy = 0; dy < h; ++dy)
        for (int dx = 0; dx < w; ++dx)
            n += c.pixels()[static_cast<size_t>(y0 + dy) * DESIGN_W + x + dx] !=
                 c.pixels()[static_cast<size_t>(y1 + dy) * DESIGN_W + x + dx];
    return n;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <new run directory>\n", argv[0]);
        return 2;
    }
    const std::filesystem::path dir = std::filesystem::path(argv[1]);
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir / "data");

    // ── SCREEN in settings.json: off by default and not written, on survives a round trip ──
    {
        StdFileSystem fs((dir / "data").string());
        SettingsValues values;
        Theme theme = theme_default();
        std::string saved;
        check(!values.wideScreen && save_settings(fs, values, theme) && fs.read_file(fs.settings_path(), saved) &&
                  saved.find("wideScreen") == std::string::npos,
              "4:3 is the default and settings.json does not mention it");
        values.wideScreen = true;
        SettingsValues loaded;
        check(save_settings(fs, values, theme) && load_settings(fs, loaded, theme) && loaded.wideScreen,
              "SCREEN 16:9 survives a save and load");
        // TOUCH BUTTONS replaces touchButtonsWithPad and keeps its saved value.
        const auto touch = [&](const char* json) {
            fs.write_file(fs.settings_path(), json);
            SettingsValues v;
            load_settings(fs, v, theme);
            return v.touchButtons;
        };
        check(touch("{\"touchButtonsWithPad\":true}") == TOUCH_ALWAYS, "old touchButtonsWithPad true reads as ALWAYS");
        check(touch("{\"touchButtonsWithPad\":false}") == TOUCH_AUTO, "...false as AUTO");
        check(touch("{\"touchButtons\":2,\"touchButtonsWithPad\":false}") == TOUCH_OFF, "a saved OFF is kept");
        check(SettingsValues{}.touchButtons == TOUCH_AUTO, "a new install gets AUTO");
    }

    set_design_width(true);
    check(DESIGN_W == 854 && wide_dx() == 107 && right_bar_x() == 729, "16:9: an 854-wide canvas, the right bar at 729");

    // Track 1 plays phrase 0 (the one on screen), track 2 phrase 1, at song row 0 and chain row 0.
    auto project = songcore::make_default_project();
    project.tracks[0].chainRefs = {0};
    project.tracks[1].chainRefs = {1};
    project.chains[0].phraseRefs[0] = 0;
    project.chains[1].phraseRefs[0] = 1;
    project.phrases[0].steps[5].note = songcore::Note::C4();
    project.phrases[1].steps[5].note = songcore::Note{2, 4};   // D-4
    AppState state;
    state.project = &project;
    state.theme = theme_classic();
    state.currentScreen = ScreenType::PHRASE;
    state.currentPhrase = 0;
    state.settings.navSongRelative = true;
    state.songCursorRow = 0;
    state.songCursorColumn = 1;   // track 1
    state.chainCursorRow = 0;
    state.currentChain = 0;

    TrackerLayout layout;
    auto canvas = std::make_unique<Canvas>();
    layout.draw(*canvas, state);
    write_bmp(*canvas, dir / "phrase-wide.bmp");

    // The side view's first column is track 2: D-4 on step 5, `---` on step 6.
    const int side = SIDE_SPACER + (EDITOR_CLIP_RIGHT - SIDE_SPACER) + 6;
    const int row5 = 82 + ROW_HEIGHT + 14 + ROW_HEIGHT + 5 * ROW_HEIGHT;   // EDITOR_Y-based, as the editor
    check(differing(*canvas, side, row5, row5 + ROW_HEIGHT, 28, ROW_HEIGHT) > 0,
          "PHRASE side view: track 2's D-4 on step 5 differs from the --- under it");
    check(differing(*canvas, side, row5 + ROW_HEIGHT, row5 + 2 * ROW_HEIGHT, 28, ROW_HEIGHT) == 0,
          "...and two rows where nothing plays look alike");

    // NAV = POOL: no song cell, so nothing lines up and step 5 shows --- like the rest.
    state.settings.navSongRelative = false;
    layout.draw(*canvas, state);
    check(differing(*canvas, side, row5, row5 + 2 * ROW_HEIGHT, 28, ROW_HEIGHT) == 0,
          "NAV = POOL: the side view shows --- on every step");

    // The other screens draw without the side view; write a few to look at.
    state.settings.navSongRelative = true;
    for (ScreenType screen : {ScreenType::SONG, ScreenType::CHAIN, ScreenType::INSTRUMENT, ScreenType::MIXER,
                              ScreenType::PROJECT, ScreenType::SETTINGS}) {
        state.currentScreen = screen;
        layout.draw(*canvas, state);
        write_bmp(*canvas, dir / ("screen-" + std::to_string(static_cast<int>(screen)) + ".bmp"));
    }

    // ── The file browser: two columns of 19, scrolling a whole column at a time ──
    for (int i = 0; i < 50; ++i) {
        BrowserItem item;
        item.displayName = "kick_" + std::to_string(i);
        item.extension = "wav";
        item.sizeText = "12KB";
        item.dateText = "11-20-26";
        item.path = "/x/" + item.displayName + ".wav";
        state.fileBrowser.items.push_back(item);
    }
    FileBrowserState& b = state.fileBrowser;
    check(browser_columns() == 2 && browser_max_scroll(50) == 19, "16:9: two browser columns; 50 files scroll at most one column");
    b.cursor = 37; b.scroll = 0; browser_keep_in_view(b);
    check(b.scroll == 0, "the 38th file is still on the first screen");
    b.cursor = 38; browser_keep_in_view(b);
    check(b.scroll == 19, "the 39th file moves the list one whole column");
    b.cursor = 5; browser_keep_in_view(b);
    check(b.scroll == 0, "going back up moves it back a column");
    b.cursor = 40; b.scroll = 0; browser_keep_in_view(b);
    state.currentScreen = ScreenType::FILE_BROWSER;
    layout.draw(*canvas, state);
    write_bmp(*canvas, dir / "browser-wide.bmp");
    state.currentScreen = ScreenType::SAMPLE_EDITOR;
    layout.draw(*canvas, state);
    write_bmp(*canvas, dir / "sample-editor-wide.bmp");

    // ── Every look at 854×480: nothing drawn into the space between the editor and the rail ──
    // That space must look the same whatever screen is up; an editor spilling into it would differ.
    // (PHRASE's side view and the centred INSTRUMENT use it on purpose, so they are not among these.)
    b.items.clear();
    int spills = 0;
    for (int look = -1; look < kSceneCount; ++look) {
        state.theme = look < 0 ? theme_classic() : scene_preset(look, VisualizerType::SCOPE);
        const SceneLayout scene = scene_layout(state.theme);
        const int railW = NavigationMapModule::WIDTH, editorW = EDITOR_CLIP_RIGHT - SIDE_SPACER;
        const int editorX = scene.railLeft ? DESIGN_W - SIDE_SPACER - editorW : SIDE_SPACER;
        const int gapX = scene.railLeft ? SIDE_SPACER + railW + 4 : editorX + editorW + 4;
        const int gapW = scene.railLeft ? editorX - 8 - gapX : right_bar_x() - 4 - gapX;
        std::vector<uint32_t> first;
        for (ScreenType screen : {ScreenType::SONG, ScreenType::CHAIN, ScreenType::TABLE, ScreenType::GROOVE,
                                  ScreenType::SCALE, ScreenType::MIXER, ScreenType::INST_POOL}) {
            state.currentScreen = screen;
            layout.draw(*canvas, state);
            std::vector<uint32_t> gap;
            for (int y = 100; y < 400; ++y)
                for (int x = gapX; x < gapX + gapW; ++x)
                    gap.push_back(canvas->pixels()[static_cast<size_t>(y) * DESIGN_W + x]);
            if (first.empty()) first = gap;
            else if (gap != first) {
                ++spills;
                std::printf("      look %d: screen %d draws into the gap\n", look, static_cast<int>(screen));
            }
        }
    }
    check(spills == 0,
          "every look: no screen draws into the space between the editor and the rail");
    state.theme = theme_classic();

    // ── Exact-pixel references at 854×480 (FNV-1a over the canvas), still frames with no clock ──
    // ⚠️ A deliberate visual change updates these: run the check, read the hash it prints.
    {
        const auto fnv = [&] {
            uint64_t h = 1469598103934665603ull;
            for (int i = 0; i < DESIGN_W * DESIGN_H; ++i) { h ^= canvas->pixels()[i]; h *= 1099511628211ull; }
            return h;
        };
        struct Ref { ScreenType screen; uint64_t hash; };
        const Ref refs[] = {
            {ScreenType::SONG, 0xe28de95e4698d5dcull}, {ScreenType::PHRASE, 0xaaab766da386c55aull},
            {ScreenType::INSTRUMENT, 0x975b80f79f44e3a3ull}, {ScreenType::MIXER, 0x18bd0f3a016a447eull},
            {ScreenType::SETTINGS, 0xb002fc343b44b6faull},
        };
        for (const Ref& r : refs) {
            state.currentScreen = r.screen;
            layout.draw(*canvas, state);
            const uint64_t h = fnv();
            char hex[24];
            std::snprintf(hex, sizeof hex, "%016llx", static_cast<unsigned long long>(h));
            check(h == r.hash, "16:9 screen " + std::to_string(static_cast<int>(r.screen)) + " matches its reference (" + hex + ")");
        }
    }

    // The scene picker previews the wide layout; a few looks, rail-left and scope-bottom ones among them.
    state.currentScreen = ScreenType::PHRASE;
    for (int look : {0, 7, 15, 31}) {
        state.scenePicker = look;
        layout.draw(*canvas, state);
        write_bmp(*canvas, dir / ("picker-" + std::to_string(look) + ".bmp"));
    }
    state.scenePicker = -1;

    std::printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
