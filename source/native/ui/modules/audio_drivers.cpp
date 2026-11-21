#include "ui/modules/audio_drivers.h"

#include <algorithm>
#include <string>

#include "ui/helpers.h"

namespace pt::ui {

namespace {

constexpr int CONTENT_PAD = 10;
constexpr int TITLE_GAP   = 6;
constexpr int FOOTER_GAP  = 8;
constexpr int BOX_W       = 620;
constexpr int ROWS_MAX    = 6;

int visible_rows(const AudioDriversState& s) {
    return std::clamp(static_cast<int>(s.names.size()), 1, ROWS_MAX);
}

int box_height(const AudioDriversState& s) {
    return 2 * CONTENT_PAD + 2 * ROW_HEIGHT + TITLE_GAP + visible_rows(s) * ROW_HEIGHT +
           FOOTER_GAP + ROW_HEIGHT;
}

int box_x() { return (DESIGN_W - BOX_W) / 2; }
int box_y(const AudioDriversState& s) { return (DESIGN_H - box_height(s)) / 2; }

int list_y(const AudioDriversState& s) {
    return box_y(s) + CONTENT_PAD + 2 * ROW_HEIGHT + TITLE_GAP;
}

std::string active_name(const AudioDriversState& s) {
    if (s.names.empty()) return "NO SWITCHABLE OUTPUT";
    const int index = std::clamp(s.active, 0, static_cast<int>(s.names.size()) - 1);
    return s.names[static_cast<size_t>(index)];
}

}  // namespace

void draw_audio_drivers(Canvas& c, const AudioDriversState& s, const Theme& t) {
    if (!s.isOpen) return;

    // Modal: not clipped by the screen beneath.
    c.reset_clip();
    draw_modal_backdrop(c);
    draw_modal_box(c, box_x(), box_y(s), BOX_W, box_height(s), t);

    const int title_y = box_y(s) + CONTENT_PAD + TEXT_PADDING;
    const char* title = "AUDIO DRIVERS";
    const int title_w = Canvas::text_width(title, CHAR_SPACING, FONT_SCALE);
    c.draw_text(title, box_x() + (BOX_W - title_w) / 2, title_y, t.textTitle, CHAR_SPACING,
                FONT_SCALE);

    // "NOW: <name>" under the title, clipped to the panel.
    const std::string playing = "NOW: " + Canvas::clip_text(active_name(s), 28);
    const int playing_w = Canvas::text_width(playing, CHAR_SPACING, FONT_SCALE);
    c.draw_text(playing, box_x() + (BOX_W - playing_w) / 2, title_y + ROW_HEIGHT, t.textParam,
                CHAR_SPACING, FONT_SCALE);

    const int rows = visible_rows(s);
    const int first = std::max(0, s.cursor - rows + 1);
    const int name_x = box_x() + 2 * CHAR_W;
    for (int i = 0; i < rows; ++i) {
        const int index = first + i;
        if (index >= static_cast<int>(s.names.size())) break;
        const int y = list_y(s) + i * ROW_HEIGHT + TEXT_PADDING;
        const bool cursor = s.is_on(index);
        const bool active = index == s.active;

        // The row's own mark: the output playing now, independent of the cursor.
        c.draw_text(active ? "*" : " ", box_x() + CONTENT_PAD, y, t.textTitle, CHAR_SPACING,
                    FONT_SCALE);
        // The row band glides to the cursor row (the same highlight the cards use), the name sits on it.
        if (cursor) c.mark_cursor(box_x() + CONTENT_PAD, y - TEXT_PADDING, BOX_W - 2 * CONTENT_PAD, ROW_HEIGHT);
        draw_cursor_cell(c, Canvas::clip_text(s.names[static_cast<size_t>(index)], active ? 28 : 34), name_x, y,
                         cursor, t.textValue, t);
        if (active) {
            const char* now = "NOW";
            const int w = Canvas::text_width(now, CHAR_SPACING, FONT_SCALE);
            c.draw_text(now, box_x() + BOX_W - CONTENT_PAD - w, y, t.textValue, CHAR_SPACING,
                        FONT_SCALE);
        }
    }
    if (s.names.empty()) {
        c.draw_text("No selectable audio outputs", name_x, list_y(s) + TEXT_PADDING, t.textEmpty,
                    CHAR_SPACING, FONT_SCALE);
    }

    const int footer_y = box_y(s) + box_height(s) - CONTENT_PAD - ROW_HEIGHT + TEXT_PADDING;
    // "PLAYING: <name>" is already under the title: the footer only speaks for what that line does not say
    // (SWITCHING..., an error) and otherwise shows the keys.
    const bool repeatsTitle = s.messageOk && s.message == "PLAYING: " + active_name(s);
    if (!s.message.empty() && !repeatsTitle) {
        c.draw_text(Canvas::clip_text(s.message, 34), box_x() + CONTENT_PAD, footer_y,
                    s.messageOk ? t.textValue : t.rowCursor, CHAR_SPACING, FONT_SCALE);
    } else {
        const char* keys = "UP/DOWN CHOOSE   A SELECT   B CLOSE";
        const int w = Canvas::text_width(keys, CHAR_SPACING, FONT_SCALE);
        c.draw_text(keys, box_x() + (BOX_W - w) / 2, footer_y, t.textParam, CHAR_SPACING,
                    FONT_SCALE);
    }
}

}  // namespace pt::ui
