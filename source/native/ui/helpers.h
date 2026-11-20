#pragma once
#include "ui/scene_presets.h"

// The layout grid every screen is built on, and the cell painters the editors share.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#include "canvas.h"
#include "theme.h"
#include "color_space.h"
#include "theme_rules.h"   // theme_polarity — the mascot reads the same one the roller does
#include "songcore/effects.h"
#include "songcore/model.h"

namespace pt::ui {

// Text is the 5×5 font at 3×; a row is 15 px of glyph plus 3 px above and below.
inline constexpr int FONT_SCALE   = 3;
inline constexpr int CHAR_SPACING = 2;
inline constexpr int ROW_HEIGHT   = 21;
inline constexpr int TEXT_PADDING = 3;

// One character slot, 17 px.
inline constexpr int CHAR_W = 5 * FONT_SCALE + CHAR_SPACING;

// The 620 px content column is centred in 640; modules are separated by 6 px.
inline constexpr int SCREEN_SPACER = 6;
inline constexpr int SIDE_SPACER   = 10;

using songcore::effect_name;
using songcore::effect_value_max;
using songcore::hex2;

// `v` into lo..hi. Unlike std::clamp, defined when hi < lo (an empty list's `count - 1`).
inline int clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Uppercase hex, zero-padded to `digits` — never truncated, so a longer value prints in full.
inline std::string hex_padded(int64_t v, size_t digits) {
    static const char* H = "0123456789ABCDEF";
    std::string s;
    for (uint64_t u = static_cast<uint64_t>(v); u != 0; u >>= 4) s.insert(s.begin(), H[u & 0xF]);
    if (s.size() < digits) s.insert(0, digits - s.size(), '0');
    return s;
}
using songcore::note_name;

inline std::string hex1(int v) {
    static const char* H = "0123456789ABCDEF";
    return std::string(1, H[v & 0x0F]);
}

// Two decimal digits — the MIDI channel, 1..16 as devices number it.
inline std::string dec2(int v) {
    std::string s = std::to_string(v);
    return s.size() >= 2 ? s : "0" + s;
}

// Replace the alpha channel, keeping RGB.
inline Argb with_alpha(Argb c, float alpha) {
    const float a = alpha < 0.0f ? 0.0f : (alpha > 1.0f ? 1.0f : alpha);
    const Argb  v = static_cast<Argb>(a * 255.0f + 0.5f);   // round
    return (v << 24) | (c & 0x00FFFFFFu);
}

// ─── Reference colours (TrackWerk 1.2) ───────────────────────────────────────────────────────────

// A reference number's own colour: chain numbers on SONG, phrase numbers on CHAIN. The theme's accent
// turned round the OkLch hue circle by the golden angle per number, so neighbours differ, the same
// number always looks the same, and every palette keeps its own lightness. A grey accent gets a
// little chroma so it still yields a hue.
namespace ref_color_detail {
// One OkLch walk per accent and start angle, cached: a SONG screen asks for up to 128 colours a frame,
// and the conversion (cube roots, a gamut walk) is real work on a Cortex-A7. A new accent (another
// theme) clears it and only the ids asked for are computed again; ids beyond the table are computed
// directly. UI thread only.
struct Table {
    Argb accent = 0;
    bool have[256] = {};
    Argb colours[256] = {};
};
inline Argb walk(Argb accent, double startDeg, int id) {
    OkLch c = to_oklch(accent);
    c.C = c.C < 0.08 ? 0.08 : c.C;
    c.H = std::fmod(c.H + startDeg + 137.50776 * id, 360.0);
    if (c.H < 0) c.H += 360.0;
    return to_argb(c);
}
inline Argb lookup(Table& table, Argb accent, double startDeg, int id) {
    if (id < 0 || id >= 256) return walk(accent, startDeg, id);
    if (table.accent != accent) {   // another theme: forget, and fill again only what is asked for
        std::fill(std::begin(table.have), std::end(table.have), false);
        table.accent = accent;
    }
    if (!table.have[id]) {
        table.colours[id] = walk(accent, startDeg, id);
        table.have[id] = true;
    }
    return table.colours[id];
}
}  // namespace ref_color_detail

inline Argb ref_color(const Theme& t, int id) {
    static ref_color_detail::Table table;
    return ref_color_detail::lookup(table, t.rowCursor, 0.0, id);
}

// An instrument's own colour, wherever it appears (PHRASE INST, the pool, the INSTRUMENT title): the
// same golden-angle walk as ref_color, started opposite on the hue circle so instrument 00 and
// chain 00 do not look alike.
inline Argb inst_color(const Theme& t, int id) {
    static ref_color_detail::Table table;
    return ref_color_detail::lookup(table, t.rowCursor, 180.0, id);
}

// The chip behind a reference cell: a faint tint and a bright edge at its left.
inline void draw_ref_chip(Canvas& c, int x, int y, int w, int h, Argb ink) {
    c.fill_rect(x, y, w, h, with_alpha(ink, .16f));
    c.fill_rect(x, y, 2, h, with_alpha(ink, .85f));
}

// ─── Modal chrome ────────────────────────────────────────────────────────────────────────────────

// A modal box's border, in the title colour. It grows outward, so a box needs `MODAL_BORDER - 1` px of
// screen beyond its rect.
inline constexpr int MODAL_BORDER = 3;

// A modal box: fill and border. The dim behind it is per modal (`modal_backdrop_active`).
inline void draw_modal_box(Canvas& c, int x, int y, int boxW, int boxH, const Theme& t) {
    c.fill_rect(x, y, boxW, boxH, t.background);
    // Ring 0 is on the box's own rect; the rest step outward.
    for (int r = 0; r < MODAL_BORDER; ++r)
        c.stroke_rect(x - r, y - r, boxW + 2 * r, boxH + 2 * r, t.textTitle);
}

// The full-canvas dim. ⚠️ A modal using it belongs in `modal_backdrop_active` too, or the letterbox
// bars stay bright.
inline void draw_modal_backdrop(Canvas& c) { c.fill_rect(-c.origin_x(), 0, DESIGN_W, DESIGN_H, MODAL_BACKDROP); }

// ─── Row background ──────────────────────────────────────────────────────────────────────────────

// A grid row's background: the every-4th accent, else plain. Cursor and selection are per cell
// (`RowCells`), playback a marker per track.
inline Argb row_bg_color(int index, const Theme& t) {
    return (index % 4 == 0) ? t.rowEvery4th : t.background;
}

// ⚠️ The cursor's two inks are opposites: text inside its bar is `background` (the cell inverts), and
// text marking its row or column with no bar is `rowCursor`. Neither is a theme row of its own.
inline Argb cursor_cell_ink(const Theme& t) { return t.background; }
inline Argb cursor_mark_ink(const Theme& t) { return t.rowCursor; }

// Text inside a selected cell.
inline Argb selection_cell_ink(const Theme& t) { return t.rowEvery4th; }

// The mascot's colours. The art is a light figure, so on a light palette it is punched out of a patch
// of TXT TITLE instead. Polarity is `background`'s, never the panel's.
struct MascotInk {
    Argb figure;     // the lit pixels
    Argb backdrop;   // filled behind the sprite first, only when `inverted`
    bool inverted;
};
inline MascotInk mascot_ink(const Theme& t, Argb panel) {
    return (theme_polarity(t) < 0) ? MascotInk{panel, t.textTitle, true}
                                   : MascotInk{t.textTitle, panel, false};
}

// A column header, lit while the cursor is in `lo`..`hi` (an FX header spans two columns).
inline Argb header_color(int cursor_column, int lo, int hi, const Theme& t) {
    return (cursor_column >= lo && cursor_column <= hi) ? cursor_mark_ink(t) : t.textParam;
}

// ─── The playback marker ─────────────────────────────────────────────────────────────────────────

// The playhead: a `>` ahead of the cell, one per track, in `textPlayhead` as set.
inline void draw_playhead(Canvas& c, int x, int text_y, const Theme& t) {
    c.draw_text(">", x, text_y, t.textPlayhead, CHAR_SPACING, FONT_SCALE);
}

// The row that is playing, behind its text: a band in `textPlayhead`, so the eye can follow the
// sequence from across the room. Scene families draw it in their own idiom — Raster, Crack intro
// and Vector as a copper bar (brightest at the centre line, fading to both edges), the terminal-like
// families as a phosphor block with hard top and bottom edges; other themes as a quiet band. Only
// drawn while a playhead is on the row, so an idle screen costs nothing.
// The rows the playhead has just left, fading like a phosphor trail: `behind` 1 and 2 rows. Drawn
// only while a playhead runs, so it reads as motion between the steps rather than jumps.
inline void draw_playback_trail(Canvas& c, int x, int y, int w, int h, const Theme& t, int behind) {
    if (behind < 1 || behind > 2) return;
    c.fill_rect(x, y, w, h, with_alpha(t.textPlayhead, behind == 1 ? .075f : .035f));
}

// The playhead glides from row to row (PHRASE, CHAIN, TABLE): while it is between two rows a soft
// band with bright edges sweeps down (Canvas::tween, forward only, so a wrap to the top jumps).
// Settled, without a clock or with Motion off nothing is added: only the row's own band shows.
inline void draw_playhead_glide(Canvas& c, uint32_t key, int x, int top, int w, int rowH, int row, const Theme& t) {
    if (row < 0) return;
    const int target = top + row * rowH;
    const int shown = c.tween(key, target, /*forwardOnly=*/true);
    if (shown == target) return;
    c.fill_rect(x, shown, w, rowH, with_alpha(t.textPlayhead, .12f));
    c.fill_rect(x, shown, w, 1, with_alpha(t.textPlayhead, .55f));
    c.fill_rect(x, shown + rowH - 1, w, 1, with_alpha(t.textPlayhead, .35f));
}

inline void draw_playback_row(Canvas& c, int x, int y, int w, int h, const Theme& t) {
    const int family = t.sceneChrome > 0 ? static_cast<int>(scene_look(t.sceneChrome - 1).family) : -1;
    const bool copper = family == 5 || family == 6 || family == 7;   // RASTER, CRACKTRO, VECTOR
    if (copper) {
        const float half = h * .5f;
        for (int line = 0; line < h; ++line) {
            const float d = std::fabs((line + .5f) - half) / half;   // 0 centre … 1 edge
            const float glow = (1.f - d) * (1.f - d);
            c.fill_rect(x, y + line, w, 1, with_alpha(mix_rgb(t.textPlayhead, t.textTitle, int(40 * glow)), .05f + .30f * glow));
        }
        return;
    }
    if (family >= 0) {
        c.fill_rect(x, y, w, h, with_alpha(t.textPlayhead, .14f));
        c.fill_rect(x, y, w, 1, with_alpha(t.textPlayhead, .55f));
        c.fill_rect(x, y + h - 1, w, 1, with_alpha(t.textPlayhead, .55f));
        return;
    }
    c.fill_rect(x, y, w, h, with_alpha(t.textPlayhead, .12f));
}

// An error message over the scope strip: red that stays legible on the theme's own scope ground
// (pure 0xFFFF0000 vanished into light scenes such as INK ON PAPER).
inline Argb error_ink(const Theme& t) {
    for (Argb red : {0xFFFF5A5Au, 0xFFE0303Au, 0xFFB0101Eu, 0xFF7A0010u})
        if (wcag_contrast(red, t.vizBackground) >= 4.5) return red;
    return t.textValue;
}

// Is a LIVE queue marker lit this frame? Slow: waiting for the chain's end; fast: the next phrase.
inline bool blink_on(int phase_ms, bool fast) {
    const int period = fast ? 200 : 600;
    return (phase_ms % period) * 2 < period;
}

// ─── Cells ───────────────────────────────────────────────────────────────────────────────────────

// A grid row's cells, left to right. The selection's background outranks the cursor's (the cursor is
// inside the block it drags) but the cursor's ink wins. An object so adjacent selected cells fill the
// gutter between them into one block. ⚠️ Cells come in column order, and each column's text is
// fixed-width or the highlight is ragged.
class RowCells {
  public:
    RowCells(Canvas& c, int text_y, const Theme& t) : c_(c), textY_(text_y), t_(t) {}

    void cell(const std::string& text, int x, bool is_cursor, bool is_selected, bool is_empty,
              Argb value_color) {
        const int left  = x - CHAR_SPACING;
        const int right = x + Canvas::text_width(text, CHAR_SPACING, FONT_SCALE) + CHAR_SPACING;

        if (is_cursor || is_selected) {
            // The gutter is claimed only between two selected cells.
            const int from = (is_selected && selectedRight_ >= 0) ? selectedRight_ : left;
            c_.fill_rect(from, textY_ - TEXT_PADDING, right - from, ROW_HEIGHT,
                         is_selected ? t_.rowSelection : t_.rowCursor);
            if (is_cursor) c_.mark_cursor(from, textY_ - TEXT_PADDING, right - from, ROW_HEIGHT);
            if (is_cursor && !is_selected) {
                const int top = textY_ - TEXT_PADDING;
                if (t_.sceneChrome)
                    c_.scene_edge(from, top, right - from, ROW_HEIGHT,
                                  mix_rgb(t_.rowCursor, t_.background, 54));
                else
                    c_.fill_rect(from, top + ROW_HEIGHT - 1, right - from, 1, darken(t_.rowCursor, .68f));
            }
        }

        selectedRight_ = is_selected ? right : -1;

        const Argb color = is_cursor     ? cursor_cell_ink(t_)
                           : is_selected ? selection_cell_ink(t_)
                           : is_empty    ? t_.textEmpty
                                         : value_color;
        c_.draw_text(text, x, textY_, color, CHAR_SPACING, FONT_SCALE);
    }

  private:
    Canvas&      c_;
    int          textY_;
    const Theme& t_;
    int          selectedRight_ = -1;   // −1: the cell to the left is not selected
};

// One cell with no neighbours.
inline void draw_cell(Canvas& c, const std::string& text, int x, int text_y, bool is_cursor,
                      bool is_selected, bool is_empty, Argb value_color, const Theme& t) {
    RowCells{c, text_y, t}.cell(text, x, is_cursor, is_selected, is_empty, value_color);
}

// One cell on a screen with no selection (parameters, buttons), with the grids' cursor.
inline void draw_cursor_cell(Canvas& c, const std::string& text, int x, int text_y, bool is_cursor,
                             Argb color, const Theme& t) {
    draw_cell(c, text, x, text_y, is_cursor, /*is_selected=*/false, /*is_empty=*/false, color, t);
}

// An EQ slot plus the ">" that says it opens the EQ editor (`show_arrow` false hides it).
inline void draw_eq_cell(Canvas& c, int value_x, int text_y, int eq_slot, bool is_cursor,
                         const Theme& t, bool show_arrow = true) {
    const std::string eq_str = (eq_slot < 0) ? "--" : hex2(eq_slot);

    // The cursor spans value and arrow, on the same edges as `RowCells`.
    if (is_cursor) {
        const std::string span = show_arrow ? eq_str + ">" : eq_str;
        const int         w = Canvas::text_width(span, CHAR_SPACING, FONT_SCALE) + 2 * CHAR_SPACING;
        c.fill_rect(value_x - CHAR_SPACING, text_y - TEXT_PADDING, w, ROW_HEIGHT, t.rowCursor);
        c.mark_cursor(value_x - CHAR_SPACING, text_y - TEXT_PADDING, w, ROW_HEIGHT);
    }

    const Argb value_color   = is_cursor      ? cursor_cell_ink(t)
                               : (eq_slot < 0) ? t.textEmpty
                                               : t.textValue;
    c.draw_text(eq_str, value_x, text_y, value_color, CHAR_SPACING, FONT_SCALE);
    if (show_arrow) {
        c.draw_text(">", value_x + 2 * CHAR_W, text_y, is_cursor ? cursor_cell_ink(t) : t.textValue,
                    CHAR_SPACING, FONT_SCALE);
    }
}

// "12.4 MB". Integer tenths: `%.1f` prints a comma under some locales.
inline std::string megabytes_str(int64_t bytes) {
    const int64_t tenths = (bytes * 10 + 524288) / 1048576;   // round to nearest tenth of a MiB
    return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + " MB";
}

// Clear one FX slot (1..3) of a step.
inline void clear_effect(songcore::PhraseStep& step, int fx_slot) {
    switch (fx_slot) {
        case 1: step.fx1Type = 0x00; step.fx1Value = 0x00; break;
        case 2: step.fx2Type = 0x00; step.fx2Value = 0x00; break;
        case 3: step.fx3Type = 0x00; step.fx3Value = 0x00; break;
        default: break;
    }
}

}  // namespace pt::ui
