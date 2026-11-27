#include "ui/modules/help_overlay.h"

#include <string>
#include <vector>

namespace pt::ui {

using namespace help_overlay_geometry;

namespace {

// The large sprite as horizontal runs (see help_panel.cpp).
void draw_large_mascot(Canvas& c, int x, int y, const MascotInk& ink) {
    // One fill for the patch, before the runs.
    if (ink.inverted) c.fill_rect(x, y, MASCOT_LARGE_W, MASCOT_LARGE_H, ink.backdrop);
    const Argb color = ink.figure;

    for (int row = 0; row < MASCOT_LARGE_H; ++row) {
        int runStart = -1;
        // ⚠️ Inclusive, so a run reaching the right edge closes.
        for (int col = 0; col <= MASCOT_LARGE_W; ++col) {
            const bool lit = (col < MASCOT_LARGE_W) && mascot_large_pixel(col, row);
            if (lit && runStart < 0) {
                runStart = col;
            } else if (!lit && runStart >= 0) {
                c.fill_rect(x + runStart, y + row, col - runStart, 1, color);
                runStart = -1;
            }
        }
    }
}

// A key line's first word, when it names buttons ("A+B", "SELECT+←/→", "L+D-PAD"): each button is drawn
// as a small key cap in the same character cells, so the layout does not move.
bool button_part(const std::string& p) {
    static const char* const names[] = {"A", "B", "L", "R", "START", "SELECT", "D-PAD", "A,A",
                                        "←", "→", "↑", "↓", "D-pad"};
    for (const char* n : names) if (p == n) return true;
    return false;
}

bool button_combo(const std::string& word, std::vector<std::string>& caps) {
    caps.clear();
    std::string part;
    const auto flush = [&] {
        if (part.empty() || !button_part(part)) return false;
        caps.push_back(part);
        part.clear();
        return true;
    };
    for (size_t i = 0; i < word.size(); ++i) {
        if (word[i] == '+' || word[i] == '/') {
            if (!flush()) return false;
            caps.push_back(std::string(1, word[i]));
        } else {
            part += word[i];
        }
    }
    return flush();
}

// Code points, as the flow counts them (each arrow is one).
int cells(const std::string& s) {
    int n = 0;
    for (size_t i = 0; i < s.size(); ++i) if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) ++n;
    return n;
}

void draw_key_caps(Canvas& c, const std::vector<std::string>& caps, int x, int y, const Theme& t) {
    for (const std::string& cap : caps) {
        const int w = cells(cap) * CHAR_W;
        if (cap == "+" || cap == "/") {
            c.draw_text(cap, x, y + TEXT_PADDING, t.textEmpty, CHAR_SPACING, FONT_SCALE);
        } else {
            c.panel_rect(x - 2, y + 1, w + 2, ROW_HEIGHT - 2, with_alpha(t.textValue, .16f),
                         with_alpha(t.textValue, .45f), 3);
            c.draw_text(cap, x, y + TEXT_PADDING, t.textTitle, CHAR_SPACING, FONT_SCALE);
        }
        x += w;
    }
}

}  // namespace

void HelpOverlayModule::draw(Canvas& c, HelpTopic topic, const Theme& t, int scroll, const HelpEntry* more,
                             const char* moreTitle) const {
    const Canvas::OriginScope inMiddle(c, wide_dx());   // a 640-wide dialog, in the middle
    const HelpEntry& e = help_entry(topic);

    const int rows      = help_overlay_rows(e, more, moreTitle);
    const int maxScroll = help_overlay_max_scroll(e, more, moreTitle);
    if (scroll > maxScroll) scroll = maxScroll;
    if (scroll < 0) scroll = 0;
    const int visible = rows < max_visible_rows() ? rows : max_visible_rows();
    const int boxH    = box_h(visible);
    const int boxY    = box_y(boxH);

    c.reset_clip();
    draw_modal_backdrop(c);
    draw_modal_box(c, BOX_X, boxY, BOX_W, boxH, t);

    // ⚠️ Invert against `meterBackground`, the ground this modal stands on.
    draw_large_mascot(c, MASCOT_X, boxY + MASCOT_DY, mascot_ink(t, t.meterBackground));

    const std::string title(e.line1, static_cast<size_t>(help_title_length(e.line1)));
    c.draw_text(title, row_x(0), boxY + row_dy(0) + TEXT_PADDING, t.textTitle, CHAR_SPACING,
                FONT_SCALE);

    // Rows under the mascot scroll inside their own window; the rows beside it stay.
    const int windowTop = boxY + row_dy(BESIDE_ROWS);
    const int windowBottom = boxY + row_dy(visible - 1) + ROW_HEIGHT;
    std::vector<std::string> caps;
    help_overlay_flow(e, [&](int row, int col, const char* word, int bytes, bool isKey) {
        int drawRow = row;
        if (row >= BESIDE_ROWS) {
            drawRow = row - scroll;
            if (drawRow < BESIDE_ROWS || drawRow >= visible) return;
        }
        const std::string text(word, static_cast<size_t>(bytes));
        const int x = row_x(drawRow) + col * CHAR_W, y = boxY + row_dy(drawRow);
        // A group heading in the key list ("MOVE:"): one capitalised word with a colon, in the title colour.
        if (isKey && col == 0 && help_key_is_heading(text.c_str())) {
            c.draw_text(text, x, y + TEXT_PADDING, t.textTitle, CHAR_SPACING, FONT_SCALE);
            return;
        }
        if (isKey && col == 0 && button_combo(text, caps)) {
            draw_key_caps(c, caps, x, y, t);
            return;
        }
        c.draw_text(text, x, y + TEXT_PADDING, isKey ? t.textEmpty : t.textValue, CHAR_SPACING, FONT_SCALE);
    }, true, help_overlay_scrolls(e, more, moreTitle), more, moreTitle);

    // More text above or below: a thin bar on the right edge, and an arrow at the end that hides text.
    if (maxScroll > 0) {
        const int trackX = BOX_X + BOX_W - PAD / 2 - 3, trackH = windowBottom - windowTop;
        c.fill_rect(trackX, windowTop, 3, trackH, with_alpha(t.textParam, .18f));
        const int thumbH = trackH * (visible - BESIDE_ROWS) / (rows - BESIDE_ROWS);
        const int thumbY = windowTop + (trackH - thumbH) * scroll / maxScroll;
        c.fill_rect(trackX, thumbY, 3, thumbH > 6 ? thumbH : 6, t.textTitle);
        const int arrowX = trackX - CHAR_W;
        if (scroll > 0) c.draw_text("↑", arrowX, windowTop + TEXT_PADDING, t.textTitle, CHAR_SPACING, FONT_SCALE);
        if (scroll < maxScroll)
            c.draw_text("↓", arrowX, windowBottom - ROW_HEIGHT + TEXT_PADDING, t.textTitle, CHAR_SPACING, FONT_SCALE);
    }

    const char* HINT = maxScroll > 0 ? "↑/↓ SCROLL   A QUICK   B BACK" : "A QUICK   B / SELECT BACK";
    const int hintW = cells(HINT) * CHAR_W - CHAR_SPACING;
    c.draw_text(HINT, BOX_X + (BOX_W - hintW) / 2, boxY + hint_dy(boxH) + TEXT_PADDING, t.textEmpty,
                CHAR_SPACING, FONT_SCALE);
}

}  // namespace pt::ui
