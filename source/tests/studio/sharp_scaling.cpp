// ptsharp-scaling-check: SETTINGS > SCALING's SHARP mode (specs/sharp-scaling.md). The size maths
// (the FIT rectangle, SHARP's whole-number enlargement N and its texture cap) at 4:3 and at the
// 854×480 wide design, and settings.json: an old scalingBilinear true/false loads as BILINEAR/INT,
// a new install gets SHARP. Builds with GCC and MSVC; no assert.
// usage: ptsharp-scaling-check <new run directory>
#include "ui/modules/settings_editor.h"
#include "ui/scaling_math.h"
#include "ui/settings_store.h"
#include "ui/std_filesystem.h"

#include <cstdio>
#include <filesystem>
#include <string>

using namespace pt::ui;

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}

// One output size: the FIT/SHARP rectangle and N, with a texture limit no GPU here would hit.
void size(int srcW, int srcH, int outW, int outH, int wantW, int wantH, int wantN) {
    int w = 0, h = 0;
    fit_size(srcW, srcH, outW, outH, w, h);
    const int n = sharp_factor(srcW, srcH, w, h, 16384, 16384);
    check(w == wantW && h == wantH && n == wantN && srcW * n >= w && srcH * n >= h,
          std::to_string(srcW) + "x" + std::to_string(srcH) + " on " + std::to_string(outW) + "x" +
              std::to_string(outH) + ": frame " + std::to_string(w) + "x" + std::to_string(h) + ", N=" +
              std::to_string(n) + " (" + std::to_string(srcW * n) + "x" + std::to_string(srcH * n) + ")");
}

int load_scaling(StdFileSystem& fs, const char* json) {
    if (json) fs.write_file(fs.settings_path(), json);
    else std::filesystem::remove(fs.settings_path());
    SettingsValues values;
    Theme theme = theme_default();
    load_settings(fs, values, theme);
    return values.scaling;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <new run directory>\n", argv[0]);
        return 2;
    }

    // ── 4:3 (640×480) ──
    size(640, 480, 960, 544, 725, 544, 2);
    size(640, 480, 1280, 720, 960, 720, 2);
    size(640, 480, 1920, 1080, 1440, 1080, 3);
    size(640, 480, 2400, 1080, 1440, 1080, 3);
    size(640, 480, 640, 480, 640, 480, 1);
    // ── wide (854×480) ──
    size(854, 480, 960, 544, 960, 539, 2);
    size(854, 480, 1280, 720, 1280, 719, 2);
    size(854, 480, 1920, 1080, 1920, 1079, 3);
    size(854, 480, 2400, 1080, 1921, 1080, 3);
    size(854, 480, 640, 480, 640, 359, 1);
    // The GPU's texture limit caps N, never below 1.
    check(sharp_factor(640, 480, 1440, 1080, 1024, 1024) == 1, "a 1024 px texture limit caps N at 1");
    check(sharp_factor(640, 480, 1440, 1080, 1920, 1920) == 3, "1920x1440 fits a 1920 px limit");

    // ── settings.json ──
    const std::filesystem::path dir = std::filesystem::path(argv[1]) / "data";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    StdFileSystem fs(dir.string());
    check(load_scaling(fs, nullptr) == SCALING_SHARP, "a new install gets SHARP");
    check(load_scaling(fs, "{\"scalingBilinear\":true}") == SCALING_BILINEAR, "old scalingBilinear true loads as BILINEAR");
    check(load_scaling(fs, "{\"scalingBilinear\":false}") == SCALING_INT, "old scalingBilinear false loads as INT");
    check(load_scaling(fs, "{\"scaling\":1,\"scalingBilinear\":false}") == SCALING_SHARP, "a saved SHARP is kept");
    check(load_scaling(fs, "{\"scaling\":7,\"scalingBilinear\":true}") == SCALING_BILINEAR,
          "a scaling out of range falls back to scalingBilinear");
    SettingsValues values;
    values.scaling = SCALING_BILINEAR;
    Theme theme = theme_default();
    std::string saved;
    check(save_settings(fs, values, theme) && fs.read_file(fs.settings_path(), saved) &&
              saved.find("\"scaling\": 2") != std::string::npos &&
              saved.find("\"scalingBilinear\": true") != std::string::npos,
          "SAVE writes scaling, and scalingBilinear for older builds");

    std::printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
