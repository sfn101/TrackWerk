#pragma once

// A 640×480 software framebuffer (854×480 in wide mode, specs/wide-mode.md) and the primitives the
// whole UI is drawn with: filled and stroked rects, bitmap text, a clip. The shell scales the
// finished frame onto the display.

#include <cstdint>
#include <unordered_map>
#include <string>
#include <vector>

#include "font5x5.h"
#include "theme.h"

namespace pt::ui {

inline constexpr int DESIGN_W_4_3  = 640;
inline constexpr int DESIGN_W_WIDE = 854;
// The canvas width: DESIGN_W_4_3, or DESIGN_W_WIDE when SETTINGS > SCREEN is 16:9. ⚠️ Set once at
// startup (set_design_width) before any Canvas exists, never while the app runs: every canvas,
// texture and window is sized from it.
inline int DESIGN_W = DESIGN_W_4_3;
inline constexpr int DESIGN_H = 480;
inline void set_design_width(bool wide) { DESIGN_W = wide ? DESIGN_W_WIDE : DESIGN_W_4_3; }
// How far a 640-wide block (a dialog, a centred screen) moves right to sit in the middle; 0 in 4:3.
inline int wide_dx() { return (DESIGN_W - DESIGN_W_4_3) / 2; }

// The modal dim; the shell blends the same value over the letterbox bars.
inline constexpr Argb MODAL_BACKDROP = 0xCC000000;

class Canvas {
public:
    Canvas() : px_(static_cast<size_t>(DESIGN_W) * DESIGN_H, 0xFF000000) { reset_clip(); }

    // ── The frame ────────────────────────────────────────────────────────────────────────────────

    // The whole canvas, clip ignored.
    void clear(Argb color = 0xFF000000);

    // ARGB, row-major — what the shell uploads.
    const uint32_t* pixels() const { return px_.data(); }

    // ── The cursor of this frame ─────────────────────────────────────────────────────────────────
    // Grids (RowCells) and lists mark the cell their cursor is drawn on; TrackerLayout glides a
    // highlight from where it was to there (ui/layout.cpp, apply_glide). Cleared at each frame's start.
    struct Rect { int x = 0, y = 0, w = 0, h = 0; };
    void mark_cursor(int x, int y, int w, int h) { cursorMark_ = {x + ox_, y, w, h}; }
    void clear_cursor_mark() { cursorMark_ = {}; scrollMark_ = {}; stageMark_ = {}; selectorMark_ = {}; }
    // The highlighted entry of a selector that stays on screen while pages change: the Studio tabs,
    // the screen map in the track rail. TrackerLayout glides a highlight from the last entry to it.
    struct SelectorMark { int id = -1; Rect at; };
    void mark_selector(int id, int x, int y, int w, int h) { selectorMark_ = {id, {x + ox_, y, w, h}}; }
    SelectorMark selector_mark() const { return selectorMark_; }

    // ── Values that ease to a new setting (knobs, faders, level blocks) ─────────────────────────
    // TrackerLayout sets the clock each frame; a new page or Motion off forgets every value.
    void set_clock(uint64_t nowMs, bool motion, int page);
    // What a value's graphic shows now: on a change it eases 120 ms (ease-out) from where it was to
    // `target`; without a clock or with Motion off, `target`. The number beside it shows `target`.
    // `forwardOnly`: a step back (a playhead wrapping to the top) jumps instead of sweeping back.
    int tween(uint32_t key, int target, bool forwardOnly = false);
    bool tweening(uint64_t nowMs) const { return tweenUntil_ > nowMs; }
    Rect cursor_mark() const { return cursorMark_; }
    // A list that scrolls marks its viewport and how far its content is scrolled, in pixels; `id`
    // tells lists apart. The shell slides the content from the last offset to this one (slide).
    struct ScrollMark { int id = -1; Rect view; int offset = 0; };
    void mark_scroll(int id, int x, int y, int w, int h, int offset) { scrollMark_ = {id, {x + ox_, y, w, h}, offset}; }
    ScrollMark scroll_mark() const { return scrollMark_; }

    // ── Motion over whole frames (shell/app.cpp, after an input or a page change) ────────────────
    // `previous` is DESIGN_W × DESIGN_H pixels of the last frame shown.
    // Phosphor afterglow: each pixel keeps `keep`/256 of the previous frame (0 = none). A cursor jump
    // or a scroll step leaves a short fading trail, which reads as more frames than were drawn.
    void afterglow(const uint32_t* previous, int keep);
    // Venetian-blind wipe for a page change: of every four lines, the first `shown` (1…3) are the new
    // frame and the rest still the old one.
    void blinds(const uint32_t* previous, int shown);
    // A page change in the style of the design family: where the new frame is not yet revealed at
    // `progress` (0…1), the previous frame stays. `edge` (alpha 0 = none) marks a moving boundary.
    // The WIPE_UP / WIPE_LEFT and PUSH_* kinds follow the direction of a move on the screen map:
    // PUSH_RIGHT slides the old page out to the left while the new one comes in from the right.
    enum class Transition { BLINDS, DISSOLVE, WIPE_DOWN, WIPE_RIGHT, IRIS, WIPE_UP, WIPE_LEFT,
                            PUSH_RIGHT, PUSH_LEFT, PUSH_DOWN, PUSH_UP };
    // `area` limits the change to the page's own part (TrackerLayout and Studio mark it, mark_stage);
    // the default is the whole screen.
    void transition(const uint32_t* previous, Transition kind, float progress, Argb edge = 0,
                    Rect area = {0, 0, DESIGN_W, DESIGN_H});
    // The part of the screen a page's content occupies; the chrome around it (title, scope, track
    // rail, Studio tabs) stays in place while the content changes. Cleared each frame.
    void mark_stage(int x, int y, int w, int h) { stageMark_ = {x + ox_, y, w, h}; }
    Rect stage_mark() const { return stageMark_; }
    // Smooth scrolling inside `view`: the content moved up by `distance` pixels (down if negative)
    // since `previous`, and `remaining` of that is still to go. Rows the shifted frame does not cover
    // come from `previous`. Needs |remaining| <= |distance| < view height.
    void slide(const uint32_t* previous, Rect view, int distance, int remaining);
    int             pitch_bytes() const { return DESIGN_W * 4; }

    // ── Primitives ───────────────────────────────────────────────────────────────────────────────

    // Blends src-over when `color` has alpha < 255.
    void fill_rect(int x, int y, int w, int h, Argb color);

    // Quiet, static grain and a shallow light falloff. No animation or random state.
    void set_surface_style(int style);
    void set_appearance(const Theme& theme, int userFont);
    void scene_edge(int x, int y, int w, int h, Argb color);
    void set_font_style(int style);
    int font_style() const { return fontStyle_; }
    void surface_rect(int x, int y, int w, int h, Argb base);

    // Rounded instrument-style panel, with an inside edge and a fine top highlight.
    void panel_rect(int x, int y, int w, int h, Argb base, Argb border, int radius = 5);

    // An outline inside the bounds.
    void stroke_rect(int x, int y, int w, int h, Argb color, int thickness = 1);

    // A one-pixel line, including its endpoints; clipped before rasterization.
    void line(int x0, int y0, int x1, int y1, Argb color);

    // `font_scale` multiplies the 5×5 cell (3 → the standard 15×15 text).
    void draw_glyph(const Glyph& g, int x, int y, Argb color, int font_scale);

    // UTF-8; each code point advances `5 * font_scale + spacing`.
    void draw_text(const std::string& text, int x, int y, Argb color, int spacing, int font_scale);

    // Proportional interface text. Sizes 12/18 use 14/21-pixel ink atlases;
    // intermediate sizes select the nearest atlas. Grid text keeps draw_text.
    void draw_label(const std::string& text, int x, int y, Argb color, int size = 12);
    static int label_width(const std::string& text, int size = 12);
    static std::string clip_label(const std::string& text, int maxWidth, int size = 12);

    // No trailing gap.
    static int text_width(const std::string& text, int spacing, int font_scale);

    static int glyph_count(const std::string& text);   // code points

    // At most `max_glyphs` columns, a cut marked with `…` inside the budget. Never splits a code point.
    static std::string clip_text(const std::string& text, int max_glyphs);

    // The same, keeping the end — for paths.
    static std::string clip_text_head(const std::string& text, int max_glyphs);

    // ── The origin ───────────────────────────────────────────────────────────────────────────────
    // Every x a primitive, clip or mark takes is shifted right by the origin, so a 640-wide block is
    // drawn in the middle of a wide canvas without changing its own coordinates. 0 in 4:3, where
    // nothing moves. The frame-wide operations (clear, afterglow, transition, slide) ignore it.
    int origin_x() const { return ox_; }
    class OriginScope {
    public:
        OriginScope(Canvas& c, int x) : c_(c), saved_(c.ox_) { c.ox_ = x; }   // absolute, not added
        ~OriginScope() { c_.ox_ = saved_; }
        OriginScope(const OriginScope&)            = delete;
        OriginScope& operator=(const OriginScope&) = delete;

    private:
        Canvas& c_;
        int     saved_;
    };

    // ── Clipping ─────────────────────────────────────────────────────────────────────────────────

    void set_clip(int x, int y, int w, int h);
    void reset_clip();

    // So an early `return` inside a module cannot leak a clip into the next one.
    class ClipScope {
    public:
        ClipScope(Canvas& c, int x, int y, int w, int h)
            : c_(c), cx_(c.clipX_), cy_(c.clipY_), cw_(c.clipW_), ch_(c.clipH_) {
            c.set_clip(x, y, w, h);
        }
        // The saved clip is absolute: restored as is, not through set_clip and the origin.
        ~ClipScope() { c_.clipX_ = cx_; c_.clipY_ = cy_; c_.clipW_ = cw_; c_.clipH_ = ch_; }
        ClipScope(const ClipScope&)            = delete;
        ClipScope& operator=(const ClipScope&) = delete;

    private:
        Canvas& c_;
        int     cx_, cy_, cw_, ch_;
    };

private:
    // A public primitive takes `x` in origin coordinates: it adds the origin once and draws with the
    // origin at 0, so the primitives it calls in turn do not add it again.
    class Absolute {
    public:
        Absolute(Canvas& c, int& x) : c_(c), saved_(c.ox_) { x += c.ox_; c.ox_ = 0; }
        Absolute(Canvas& c, int& x0, int& x1) : c_(c), saved_(c.ox_) { x0 += c.ox_; x1 += c.ox_; c.ox_ = 0; }
        ~Absolute() { c_.ox_ = saved_; }
        Absolute(const Absolute&)            = delete;
        Absolute& operator=(const Absolute&) = delete;

    private:
        Canvas& c_;
        int     saved_;
    };

    int ox_ = 0;
    std::vector<uint32_t> px_;
    Rect cursorMark_;
    ScrollMark scrollMark_;
    Rect stageMark_;
    SelectorMark selectorMark_;
    struct Tween { int from = 0, to = 0; uint64_t start = 0, until = 0; };
    std::unordered_map<uint32_t, Tween> tweens_;
    uint64_t clockMs_ = 0, tweenUntil_ = 0;
    bool motion_ = false;
    int tweenPage_ = -1;
    std::vector<uint32_t> slideRows_;
    std::vector<uint32_t> surfacePixels_;
    Argb                 surfaceColor_ = 0;
    int                  surfaceStyle_ = 0;
    int                  fontStyle_ = 0;
    int                  cachedSurfaceStyle_ = -1;
    int                  cachedSceneChrome_ = 0;
    int                  sceneChrome_ = 0;
    Argb                 sceneInk_ = 0;
    int                   clipX_ = 0, clipY_ = 0, clipW_ = DESIGN_W, clipH_ = DESIGN_H;
};

}  // namespace pt::ui
