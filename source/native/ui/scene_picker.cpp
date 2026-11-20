#include "ui/scene_picker.h"
#include "ui/scene_layout.h"
#include "ui/scene_animation.h"

#include <algorithm>
#include <string>

#include "ui/helpers.h"
#include "ui/step_grid.h"

namespace pt::ui {

bool scene_picker_button(Button button, int& index, Theme& theme) {
    if (index < 0) return false;
    const int family = index / kScenesPerFamily;
    const int row = index % kScenesPerFamily;
    switch (button) {
        case Button::DPAD_LEFT:
            index = ((family + kSceneFamilyCount - 1) % kSceneFamilyCount) * kScenesPerFamily + row;
            break;
        case Button::DPAD_RIGHT:
            index = ((family + 1) % kSceneFamilyCount) * kScenesPerFamily + row;
            break;
        case Button::DPAD_UP:
            index = family * kScenesPerFamily + (row + kScenesPerFamily - 1) % kScenesPerFamily;
            break;
        case Button::DPAD_DOWN:
            index = family * kScenesPerFamily + (row + 1) % kScenesPerFamily;
            break;
        case Button::A:
            theme = scene_preset(index, theme.visualizerType);
            index = -1;
            break;
        case Button::B:
            index = -1;
            break;
        case Button::START:
            return false;
        default:
            break;
    }
    return true;
}

void draw_scene_picker(Canvas& c, int index, const Theme& current, uint64_t ms) {
    const int family = index / kScenesPerFamily;
    const Theme t = scene_preset(index, current.visualizerType);
    c.set_appearance(t, 0);
    c.surface_rect(-c.origin_x(), 0, DESIGN_W, DESIGN_H, t.background);   // the whole canvas, centred or not
    c.panel_rect(16, 14, 608, 48, t.rowEvery4th, t.textParam, 4);
    c.draw_label("SCENE PRESETS", 29, 24, t.textTitle, 28);   // the title size
    c.draw_label("TRACKWERK", 504, 31, t.textParam, 14);

    for (int i = 0; i < kSceneFamilyCount; ++i) {
        // Two rows of five: ten families.
        const int x = 16 + (i % 5) * 123, y = 75 + (i / 5) * 33;
        const bool selected = i == family;
        c.panel_rect(x, y, 116, 29, selected ? t.rowCursor : t.rowEvery4th, t.textParam, 4);
        c.draw_label(scene_family_name(i), x + 8, y + 7,
                     selected ? cursor_cell_ink(t) : t.textValue, 13);
    }

    for (int i = 0; i < kScenesPerFamily; ++i) {
        const int preset = family * kScenesPerFamily + i;
        const int y = 155 + 61 * i;
        const bool selected = preset == index;
        c.panel_rect(16, y, 292, 54, t.rowEvery4th, selected ? t.rowCursor : t.textEmpty, 4);
        const auto name = std::string(kScenePresets[preset].name);
        c.draw_label(name.substr(name.find('/') + 2), 31, y + 9, selected ? t.rowCursor : t.textValue, 17);
        for (int swatch = 0; swatch < 3; ++swatch) {
            const auto& palette = kScenePresets[preset];
            const Argb colors[] = {palette.ground, palette.cursor, palette.selection};
            c.fill_rect(32 + 18 * swatch, y + 32, 14, 8, colors[swatch]);
        }
        if (current.name == name && current.sceneChrome == preset + 1)
            c.draw_label("ACTIVE", 224, y + 32, t.textParam, 12);
    }

    // Wide mode previews the wide layout: the preview grows by the canvas's own ratio (854 / 640), the
    // rail stays at its edge and the extra space opens beside the grid. It reaches into the right margin.
    const int extra = wide_dx() > 0 ? 89 : 0;
    c.panel_rect(325, 155, 299 + extra, 237, t.rowEvery4th, t.textParam, 4);
    c.draw_label("PHRASE PREVIEW", 341, 166, t.textTitle, 16);
    // Live: this look's own playback animation, as it runs while a song plays (120 BPM).
    scene_animation::family_strip(c, 548, 162, t, ms, 120, int(ms / 125) % 16);
    const auto scene = scene_layout(t);
    constexpr int previewX = 341, railWidth = 42, gridWidth = 216;
    const int gridX = previewX + (scene.railLeft ? 50 + extra : 0);
    const int railX = previewX + (scene.railLeft ? 0 : 224 + extra);
    const int scopeY = scene.scopeBottom ? 354 : 196;
    const int headerY = scene.scopeBottom ? 200 : 233;
    c.panel_rect(previewX, scopeY, 266 + extra, 23, t.background, with_alpha(t.textParam, .6f), 3);
    for (int i = 0; i < 26 + extra / 10; ++i) {
        const int height = 3 + (i * i + 5 * family) % 14;
        c.fill_rect(previewX + 4 + i * 10, scopeY + (23 - height) / 2, 5, height,
                    i < 20 ? t.vizWave : t.vizCenterLine);
    }
    c.panel_rect(railX, headerY - 3, railWidth, 115, t.background, with_alpha(t.textParam, .65f), 3);
    c.draw_label("TRK", railX + 9, headerY + 5, t.textParam, 12);
    for (int track = 0; track < 4; ++track) {
        const int y = headerY + 23 + track * 20;
        c.draw_label(std::to_string(track + 1), railX + 8, y, t.textValue, 12);
        c.fill_rect(railX + 21, y + 5, track == 0 ? 12 : 4, 3, track == 0 ? t.vizWave : t.textEmpty);
    }
    c.draw_label("STEP    NOTE     IN     FX1", gridX + 2, headerY + 3, t.textParam, 12);
    constexpr const char* notes[] = {"C-4", "G-4", "A#4", "..."};
    constexpr const char* effects[] = {"CHD03", "ARC02", ".....", "....."};
    for (int i = 0; i < 4; ++i) {
        const int y = headerY + 21 + 22 * i;
        if (i > 0) c.fill_rect(gridX, y, gridWidth, 21, t.background);
        // A playhead walks the four preview steps (an eighth note at 120 BPM): the look's playing row.
        if (i == int(ms / 250) % 4) draw_playback_row(c, gridX, y, gridWidth, 21, t);
        draw_step_grid(c, t, t.phraseGrid, i, gridX, y, gridWidth, 21,
                       {gridX + 32, gridX + 85, gridX + 123});
        c.draw_text(hex2(i), gridX + 3, y + 4, i ? t.textParam : t.textPlayhead, 1, 2);
        c.draw_text(notes[i], gridX + 40, y + 4, i ? t.textValue : t.rowCursor, 1, 2);
        c.draw_text(i == 3 ? ".." : "01", gridX + 92, y + 4, i == 3 ? t.textEmpty : t.textValue, 1, 2);
        c.draw_text(effects[i], gridX + 129, y + 4, i >= 2 ? t.textEmpty : t.textValue, 1, 2);
    }
    c.draw_label("LEFT / RIGHT  FAMILY     UP / DOWN  PRESET", 17, 422, t.textValue, 14);
    c.draw_label("A  APPLY     B  BACK", 17, 450, t.rowCursor, 14);
    c.draw_label(std::to_string(index + 1) + " / " + std::to_string(kSceneCount),
                 538, 450, t.textParam, 14);
}

}  // namespace pt::ui
