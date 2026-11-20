#include "canvas.h"
#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>   // _BitScanForward
#endif
#include "font_hd.h"
#include "font_ui.h"
#include "demo_font_style.h"
#include "demo_fonts.h"
#include "scene_presets.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace pt::ui {

namespace {

// Src-over, 8-bit integer.
inline uint32_t blend(uint32_t dst, uint32_t src) {
    const uint32_t sa = (src >> 24) & 0xFF;
    if (sa == 0xFF) return src;
    if (sa == 0) return dst;
    const uint32_t ia = 255 - sa;
    const uint32_t sr = (src >> 16) & 0xFF, sg = (src >> 8) & 0xFF, sb = src & 0xFF;
    const uint32_t dr = (dst >> 16) & 0xFF, dg = (dst >> 8) & 0xFF, db = dst & 0xFF;
    // Rounded, not truncated.
    const uint32_t r = (sr * sa + dr * ia + 127) / 255;
    const uint32_t g = (sg * sa + dg * ia + 127) / 255;
    const uint32_t b = (sb * sa + db * ia + 127) / 255;
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

inline Argb shade(Argb color, int delta) {
    const auto ch = [&](int shift) {
        return static_cast<Argb>(std::clamp(static_cast<int>((color >> shift) & 255) + delta, 0, 255));
    };
    return (color & 0xFF000000u) | ch(16) << 16 | ch(8) << 8 | ch(0);
}

// Deterministic blue-noise-like grain, small enough to leave text and data quiet.
inline int grain(int x, int y) {
    uint32_t seed = static_cast<uint32_t>(x) * 0x1f123bb5u ^ static_cast<uint32_t>(y) * 0x5f356495u;
    seed ^= seed >> 15; seed *= 0x2c1b3c6du; seed ^= seed >> 12;
    return static_cast<int>(seed % 3) - 1;
}

int material_delta(int style, int x, int y) {
    switch (style) {
        case 1: // Brushed aluminium: fine horizontal tool marks.
            return (y % 4 == 0 ? -2 : (y % 4 == 1 ? 1 : 0)) + grain(x / 5, y);
        case 2: // Interleaved bundles, alternating their thread direction.
            return ((x / 4 + y / 4) & 1) ? (x % 4 < 2 ? -2 : 1) : (y % 4 < 2 ? 2 : -1);
        case 3: { // Paper grain with occasional short, quiet fibres.
            const int fibre = y % 5 == 0 && (x + (y / 5) * 17) % 47 < 15 ? -1 : 0;
            return grain(x, y) * 2 + fibre;
        }
        case 4: { // Sparse right-angle routes and square solder pads.
            const int xx = x % 64, yy = y % 64;
            if ((x / 64 + 2 * (y / 64)) % 3 != 0) return 0;
            const bool route = (yy == 18 && xx >= 10 && xx <= 46) ||
                               (xx == 46 && yy >= 18 && yy <= 49);
            const bool pad = (xx >= 8 && xx <= 11 && yy >= 17 && yy <= 20) ||
                             (xx >= 45 && xx <= 48 && yy >= 48 && yy <= 51);
            return route || pad ? 3 : 0;
        }
        case 5: // Precision drafting grid; major rules remain only a few RGB steps.
            return x % 64 == 0 || y % 64 == 0 ? 2 : (x % 16 == 0 || y % 16 == 0 ? 1 : 0);
        case 6: { // Broad diagonal silk folds, entirely integer and deterministic.
            const int phase = (x + 2 * y) % 128;
            const int distance = std::abs(phase - 64);
            return distance * distance * 4 / 4096 - 2;
        }
        case 7: return 0;
        case 8: // Ceramic glaze: quiet, broad relief over microscopic pores.
            return grain(x / 2,y / 2) + ((x / 120 + y / 90) % 3 == 0 ? 1 : 0);
        case 9: { // Punched metal, staggered ventilation perforations.
            const int xx=(x+(y/8%2)*4)%8,yy=y%8;
            return (xx==3||xx==4)&&(yy==3||yy==4)?-3:(xx==3&&yy==5?1:0);
        }
        case 10: { // Very fine contour engraving; no animation or bitmap assets.
            const int xx=x%160-80,yy=y%120-60;
            const int ring=(xx*xx+yy*yy)/180;
            return ring%9==0?2:0;
        }
        case 11: // Anodized satin with a fine cross-grain.
            return (x%5==0?-1:0)+(y%7==0?1:0)+grain(x/7,y/3);
        case 12: { // Original printmaking-style crosshatch, with quiet open cells.
            const int diagonal=(x+y)%18,opposite=(x-y+DESIGN_H)%24;
            return (diagonal==0?3:0)+(opposite==0?2:0)+grain(x/4,y/4);
        }
        case 13: // CRT-era raster glass, static: no full-screen flashing or scrolling.
            return (y%4==0?-2:0)+(x%3==0?1:0)+(y%32==0?2:0);
        case 14: { // Perspective vector floor beneath a fine registration grid.
            if(y<210)return x%80==0&&y%8<2?2:0;
            const int depth=y-186,span=22+depth/3;
            const int perspective=(x-320)*96/std::max(1,depth);
            return perspective%64==0||y%span==0?4:0;
        }
        case 15: { // Isometric tile edges; a bespoke geometric game-era homage.
            const int a=(x+2*y)%96,b=(x-2*y+1024)%96;
            return a==0||b==0?3:((y%48==0&&((x/48+y/24)&1))?1:0);
        }
        case 16: { // Sparse 2px constellation pixels, repeatable without random state.
            const int xx=x%64,yy=y%48;const unsigned h=unsigned(x/64*73+y/48*151);
            return (xx==int(h%43)+8&&yy==int(h%29)+7)?5:
                   (xx%16==0&&yy%16==0?1:0);
        }
        case 17: { // Routed X junctions: our original Bunka circuit signature.
            const int xx=x%96,yy=y%72;
            const bool trace=(yy==18&&xx>=12&&xx<=70)||(xx==70&&yy>=18&&yy<=51);
            const bool junction=xx>=26&&xx<=38&&yy>=36&&yy<=48&&
                                (xx-26==yy-36||xx-26+yy-36==12);
            return trace||junction?4:((xx==12||xx==70)&&(yy==18||yy==51)?5:0);
        }
        default: return grain(x, y);
    }
}

// One UTF-8 code point at `i`, advancing past it. A malformed byte is consumed as U+FFFD (blank).
inline uint32_t next_codepoint(const std::string& s, size_t& i) {
    const auto b0 = static_cast<unsigned char>(s[i]);
    const size_t n = s.size();

    const auto cont = [&](size_t k) {
        return i + k < n && (static_cast<unsigned char>(s[i + k]) & 0xC0) == 0x80;
    };
    const auto bits = [&](size_t k) { return static_cast<uint32_t>(s[i + k]) & 0x3F; };

    if (b0 < 0x80) { i += 1; return b0; }
    if ((b0 & 0xE0) == 0xC0 && cont(1)) {
        const uint32_t cp = ((b0 & 0x1Fu) << 6) | bits(1);
        i += 2; return cp;
    }
    if ((b0 & 0xF0) == 0xE0 && cont(1) && cont(2)) {
        const uint32_t cp = ((b0 & 0x0Fu) << 12) | (bits(1) << 6) | bits(2);
        i += 3; return cp;
    }
    if ((b0 & 0xF8) == 0xF0 && cont(1) && cont(2) && cont(3)) {
        const uint32_t cp = ((b0 & 0x07u) << 18) | (bits(1) << 12) | (bits(2) << 6) | bits(3);
        i += 4; return cp;
    }
    i += 1;
    return 0xFFFD;
}

// Character-cell patterns are baked into the cached canvas background.
int scene_texture(int chrome, int x, int y) {
    const int index = chrome - 1;
    const int variant = scene_look(index).variant;
    const auto family = scene_look(index).family;
    const int cell = 12 + variant * 4;
    switch (family) {
        case SceneFamily::UNSCII:
            return (x / cell + y / cell) % 4 == 0 &&
                   (x % cell < 3 || y % cell < 2) ? 8 : 0;
        case SceneFamily::ASCII:
            return variant && y % cell == 0 && (x + 5 * (y / cell)) % cell < 2 ? 5 : 0;
        case SceneFamily::DOS:
            return variant == 0 ? 0 : (y % cell == 0 ? 4 : 0);
        case SceneFamily::TERMINAL:
            return variant < 2 && y % (4 + variant) == 0 ? -4 : 0;
        case SceneFamily::ANSI:
            return (x / cell + (y / cell) * 2) % 7 == 0 &&
                   x % cell < 5 && y % cell < 5 ? 9 : 0;
        case SceneFamily::RASTER:
            return y % (cell * 2) < 2 ? 7 : (y % 4 == 0 ? -3 : 0);
        case SceneFamily::CRACKTRO:
            return (x + (y / cell) * 11) % (cell * 4) < 12 && y % cell == 0 ? 8 : 0;
        case SceneFamily::VECTOR:
            return (x + y * (variant % 2 + 1)) % (cell * 3) == 0 ||
                   (x - y + 1024) % (cell * 3) == 0 ? 7 : 0;
    }
    return 0;
}

struct LabelFont {
    const detail::UiFontGlyph* glyphs;
    const uint8_t* pixels;
    int height, fallbackScale;
    int scale = 1;   // whole-pixel enlargement of the atlas (the title size)
};

// Three sizes: 14 px (up to 14), 21 px (15–25) and the title size, 28 px (26 and up) — the 14-px
// atlas drawn at twice the size, pixel for pixel, so the pixel typefaces stay crisp.
inline constexpr int kTitleLabelSize = 26;
LabelFont label_font(int size) {
    if (size >= kTitleLabelSize)
        return LabelFont{detail::uiFont12Glyphs, detail::uiFont12Pixels, detail::uiFont12Height * 2, 4, 2};
    return size >= 15 ? LabelFont{detail::uiFont18Glyphs, detail::uiFont18Pixels, detail::uiFont18Height, 3}
                      : LabelFont{detail::uiFont12Glyphs, detail::uiFont12Pixels, detail::uiFont12Height, 2};
}

int label_index(uint32_t cp) {
    if (cp >= 32 && cp < 127) return static_cast<int>(cp - 32);
    if (cp >= 160 && cp < 256) return static_cast<int>(95 + cp - 160);
    return cp == 0x2026 ? 191 : -1;
}

int label_advance(uint32_t cp, const LabelFont& font) {
    const int index = label_index(cp);
    return index >= 0 ? font.glyphs[index].advance * font.scale : 5 * font.fallbackScale;
}

}  // namespace

void Canvas::clear(Argb color) {
    std::fill(px_.begin(), px_.end(), color);
}

void Canvas::afterglow(const uint32_t* previous, int keep) {
    if (!previous || keep <= 0) return;
    keep = std::min(keep, 255);
    const uint32_t fresh = static_cast<uint32_t>(256 - keep), old = static_cast<uint32_t>(keep);
    for (size_t i = 0; i < px_.size(); ++i) {
        const uint32_t a = px_[i], b = previous[i];
        if (a == b) continue;
        // Red+blue and green in two lanes: two multiplies per pixel.
        const uint32_t rb = (((a & 0xff00ffu) * fresh + (b & 0xff00ffu) * old) >> 8) & 0xff00ffu;
        const uint32_t g = (((a & 0x00ff00u) * fresh + (b & 0x00ff00u) * old) >> 8) & 0x00ff00u;
        px_[i] = 0xff000000u | rb | g;
    }
}

void Canvas::transition(const uint32_t* previous, Transition kind, float progress, Argb edge, Rect area) {
    if (!previous || progress >= 1.f) return;
    progress = std::max(progress, 0.f);
    // The changing part of the screen; outside it the new frame shows at once. The whole screen
    // gives exactly the full-screen transition.
    const int X = std::clamp(area.x, 0, DESIGN_W), Y = std::clamp(area.y, 0, DESIGN_H);
    const int W = std::clamp(area.x + area.w, 0, DESIGN_W) - X, H = std::clamp(area.y + area.h, 0, DESIGN_H) - Y;
    if (W <= 0 || H <= 0) return;
    const auto old = [&](int y) { return previous + static_cast<size_t>(y) * DESIGN_W; };
    const auto row = [&](int y) { return px_.data() + static_cast<size_t>(y) * DESIGN_W; };
    const auto keep = [&](int y, int from, int to) {   // old pixels [from, to) of row y
        if (to > from) std::copy(old(y) + from, old(y) + to, row(y) + from);
    };
    switch (kind) {
        case Transition::BLINDS: {
            const int shown = static_cast<int>(progress * 4.f);
            for (int y = Y; y < Y + H; ++y)
                if ((y - Y) % 4 >= shown) keep(y, X, X + W);
            return;
        }
        case Transition::DISSOLVE:   // 8 × 8 cells in a fixed scrambled order
            for (int by = 0; by * 8 < H; ++by)
                for (int bx = 0; bx * 8 < W; ++bx) {
                    uint32_t h = static_cast<uint32_t>(bx * 73856093) ^ static_cast<uint32_t>(by * 19349663);
                    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
                    if (float(h & 1023u) / 1024.f < progress) continue;
                    for (int y = Y + by * 8; y < std::min(Y + H, Y + by * 8 + 8); ++y)
                        keep(y, X + bx * 8, std::min(X + W, X + bx * 8 + 8));
                }
            return;
        case Transition::WIPE_DOWN: {
            const int line = Y + static_cast<int>(progress * H);
            for (int y = line; y < Y + H; ++y) keep(y, X, X + W);
            if (edge >> 24) fill_rect(X, std::max(Y, line - 2), W, 2, edge);
            return;
        }
        case Transition::WIPE_UP: {
            const int line = Y + H - static_cast<int>(progress * H);
            for (int y = Y; y < line; ++y) keep(y, X, X + W);
            if (edge >> 24) fill_rect(X, std::min(Y + H - 2, line), W, 2, edge);
            return;
        }
        case Transition::WIPE_RIGHT: {
            const int column = X + static_cast<int>(progress * W);
            for (int y = Y; y < Y + H; ++y) keep(y, column, X + W);
            if (edge >> 24) fill_rect(std::max(X, column - 2), Y, 2, H, edge);
            return;
        }
        case Transition::WIPE_LEFT: {
            const int column = X + W - static_cast<int>(progress * W);
            for (int y = Y; y < Y + H; ++y) keep(y, X, column);
            if (edge >> 24) fill_rect(std::min(X + W - 2, column), Y, 2, H, edge);
            return;
        }
        case Transition::PUSH_RIGHT: case Transition::PUSH_LEFT: case Transition::PUSH_DOWN: case Transition::PUSH_UP: {
            // Ease-out: most of the way in the first frames, then it settles.
            const float e = 1.f - (1.f - progress) * (1.f - progress);
            slideRows_.assign(px_.begin(), px_.end());
            const auto fresh = [&](int y) { return slideRows_.data() + static_cast<size_t>(y) * DESIGN_W; };
            if (kind == Transition::PUSH_RIGHT || kind == Transition::PUSH_LEFT) {
                const int off = static_cast<int>(std::lround((1.f - e) * W));   // how far the new page still has to go
                const bool right = kind == Transition::PUSH_RIGHT;
                for (int y = Y; y < Y + H; ++y) {
                    auto* r = row(y) + X;
                    const auto* now = fresh(y) + X;
                    const auto* was = old(y) + X;
                    if (right) {   // the old page's right part, then the new page from `off` on
                        std::copy(was + (W - off), was + W, r);
                        std::copy(now, now + (W - off), r + off);
                    } else {       // the new page's right part, then the old page's left part
                        std::copy(now + off, now + W, r);
                        std::copy(was, was + off, r + (W - off));
                    }
                }
                const int seam = X + (right ? off : W - off);
                if ((edge >> 24) && off > 0) fill_rect(std::clamp(seam - 1, X, X + W - 2), Y, 2, H, edge);
            } else {
                const int off = static_cast<int>(std::lround((1.f - e) * H));
                const bool down = kind == Transition::PUSH_DOWN;
                for (int y = 0; y < H; ++y) {
                    // down: the old page's bottom part, then the new page rising; up: the reverse
                    const uint32_t* from = down ? (y < off ? old(Y + H - off + y) : fresh(Y + y - off))
                                                : (y < H - off ? fresh(Y + y + off) : old(Y + y - (H - off)));
                    std::copy(from + X, from + X + W, row(Y + y) + X);
                }
                const int seam = Y + (down ? off : H - off);
                if ((edge >> 24) && off > 0) fill_rect(X, std::clamp(seam - 1, Y, Y + H - 2), W, 2, edge);
            }
            return;
        }
        case Transition::IRIS: {     // an ellipse opening from the centre, row by row
            // Exactly the per-pixel rule (revealed where dx² + dy² <= r², ring where it also exceeds
            // (r − .02)²): a square root per row finds each boundary, and the same float test settles
            // the one or two pixels at it.
            const float r = progress * 1.15f, inner = r - .02f;
            const float rr = r * r, ii = inner * inner;
            const int cx = X + W / 2, cy = Y + H / 2;
            for (int y = Y; y < Y + H; ++y) {
                const float dy = (y - cy) / float(H / 2);
                const auto d = [&](int x) {
                    const float dx = (x - cx) / float(W / 2);
                    return dx * dx + dy * dy;
                };
                // [lo, hi) where d(x) <= limit, an interval around the centre (empty: lo == hi).
                const auto span = [&](float limit, int& lo, int& hi) {
                    if (!(d(cx) <= limit)) { lo = hi = cx; return; }
                    const float q = limit - dy * dy;
                    const int s = q > 0 ? static_cast<int>(std::sqrt(q) * (W / 2)) : 0;
                    hi = std::min(X + W, cx + s + 1);
                    while (hi < X + W && d(hi) <= limit) ++hi;
                    while (hi > cx + 1 && !(d(hi - 1) <= limit)) --hi;
                    lo = std::max(X, cx - s);
                    while (lo > X && d(lo - 1) <= limit) --lo;
                    while (lo < cx && !(d(lo) <= limit)) ++lo;
                };
                int left, right;
                span(rr, left, right);
                keep(y, X, left);
                keep(y, right, X + W);
                if (!(edge >> 24) || left == right) continue;
                int inLeft, inRight;
                span(ii, inLeft, inRight);
                if (inLeft == inRight) inLeft = inRight = right;   // no inner part: all ring
                auto* rowPx = row(y);
                for (int x = left; x < std::max(left, std::min(inLeft, right)); ++x) rowPx[x] = blend(rowPx[x], edge);
                for (int x = std::max(inRight, left); x < right; ++x) rowPx[x] = blend(rowPx[x], edge);
            }
            return;
        }
    }
}

void Canvas::set_clock(uint64_t nowMs, bool motion, int page) {
    if (nowMs == 0 || !motion || page != tweenPage_) {
        tweens_.clear();
        tweenUntil_ = 0;
    }
    clockMs_ = nowMs;
    motion_ = motion;
    tweenPage_ = page;
}

int Canvas::tween(uint32_t key, int target, bool forwardOnly) {
    if (clockMs_ == 0 || !motion_) return target;
    constexpr uint64_t kTweenMs = 120;
    const uint64_t now = clockMs_;
    const auto shown = [now](const Tween& w) {
        if (now >= w.until) return w.to;
        const float p = float(now - w.start) / float(w.until - w.start);
        const float e = 1.f - (1.f - p) * (1.f - p) * (1.f - p);
        return w.from + static_cast<int>(std::lround((w.to - w.from) * e));
    };
    auto it = tweens_.find(key);
    if (it == tweens_.end()) {   // first seen: no motion
        tweens_[key] = {target, target, now, 0};
        return target;
    }
    Tween& w = it->second;
    if (forwardOnly && target < w.to) {   // a jump back: no motion
        w = {target, target, now, 0};
        return target;
    }
    if (w.to != target) {
        w.from = shown(w);       // from wherever it is now
        w.to = target;
        w.start = now;
        w.until = now + kTweenMs;
        tweenUntil_ = std::max(tweenUntil_, w.until);
    }
    return shown(w);
}

void Canvas::slide(const uint32_t* previous, Rect view, int distance, int remaining) {
    view.w = std::min(view.w, DESIGN_W - std::max(view.x, 0));
    view.x = std::max(view.x, 0);
    const int top = std::max(view.y, 0), bottom = std::min(view.y + view.h, DESIGN_H);
    if (!previous || remaining == 0 || view.w <= 0 || bottom <= top) return;
    if (std::abs(distance) >= bottom - top || std::abs(remaining) > std::abs(distance)) return;
    // The new rows of the view, before any of them is overwritten.
    slideRows_.resize(static_cast<size_t>(bottom - top) * view.w);
    for (int y = top; y < bottom; ++y)
        std::copy(px_.begin() + static_cast<std::ptrdiff_t>(y) * DESIGN_W + view.x,
                  px_.begin() + static_cast<std::ptrdiff_t>(y) * DESIGN_W + view.x + view.w,
                  slideRows_.begin() + static_cast<std::ptrdiff_t>(y - top) * view.w);
    for (int y = top; y < bottom; ++y) {
        const int fresh = y - remaining;   // the new frame, shifted back by what is still to go
        auto* row = px_.data() + static_cast<size_t>(y) * DESIGN_W + view.x;
        if (fresh >= top && fresh < bottom) {
            const auto* from = slideRows_.data() + static_cast<size_t>(fresh - top) * view.w;
            std::copy(from, from + view.w, row);
        } else {   // not in the new frame yet: the old one, where that row sat before the scroll
            const int old = std::clamp(fresh + distance, top, bottom - 1);
            const auto* from = previous + static_cast<size_t>(old) * DESIGN_W + view.x;
            std::copy(from, from + view.w, row);
        }
    }
}

void Canvas::blinds(const uint32_t* previous, int shown) {
    if (!previous || shown >= 4) return;
    shown = std::max(shown, 0);
    for (int y = 0; y < DESIGN_H; ++y) {
        if (y % 4 < shown) continue;
        std::copy(previous + static_cast<size_t>(y) * DESIGN_W, previous + static_cast<size_t>(y + 1) * DESIGN_W,
                  px_.begin() + static_cast<std::ptrdiff_t>(y) * DESIGN_W);
    }
}

void Canvas::set_clip(int x, int y, int w, int h) {
    x += ox_;
    clipX_ = std::max(0, x);
    clipY_ = std::max(0, y);
    clipW_ = std::max(0, std::min(x + w, DESIGN_W) - clipX_);
    clipH_ = std::max(0, std::min(y + h, DESIGN_H) - clipY_);
}

void Canvas::reset_clip() {
    clipX_ = 0;
    clipY_ = 0;
    clipW_ = DESIGN_W;
    clipH_ = DESIGN_H;
}

void Canvas::fill_rect(int x, int y, int w, int h, Argb color) {
    if (w <= 0 || h <= 0) return;
    const Absolute at(*this, x);

    const int x0 = std::max(x, clipX_);
    const int y0 = std::max(y, clipY_);
    const int x1 = std::min(x + w, clipX_ + clipW_);
    const int y1 = std::min(y + h, clipY_ + clipH_);
    if (x0 >= x1 || y0 >= y1) return;

    const bool opaque = ((color >> 24) & 0xFF) == 0xFF;
    for (int py = y0; py < y1; ++py) {
        uint32_t* row = px_.data() + static_cast<size_t>(py) * DESIGN_W;
        if (opaque) {
            std::fill(row + x0, row + x1, color);
        } else {
            for (int px = x0; px < x1; ++px) row[px] = blend(row[px], color);
        }
    }
}

void Canvas::set_surface_style(int style) {
    surfaceStyle_ = std::clamp(style, 0, kSurfaceStyleCount-1);
}

void Canvas::set_appearance(const Theme& theme, int userFont) {
    set_surface_style(theme.surfaceStyle);
    set_font_style(theme_font(theme, userFont));
    sceneChrome_ = std::clamp(theme.sceneChrome, 0, kSceneCount);
    sceneInk_ = theme.textTitle;
}

void Canvas::set_font_style(int style) { fontStyle_=valid_demo_font_style(style); }

void Canvas::surface_rect(int x, int y, int w, int h, Argb base) {
    if (w <= 0 || h <= 0) return;
    const Absolute at(*this, x);
    const int x0 = std::max(x, clipX_), y0 = std::max(y, clipY_);
    const int x1 = std::min(x + w, clipX_ + clipW_), y1 = std::min(y + h, clipY_ + clipH_);
    if (x0 >= x1 || y0 >= y1) return;
    // One background per canvas: only regenerate when its theme colour changes.
    // All subsequent frames copy runs, avoiding per-pixel noise work in the draw loop.
    if (surfacePixels_.empty() || surfaceColor_ != base || cachedSurfaceStyle_ != surfaceStyle_ ||
        cachedSceneChrome_ != sceneChrome_) {
        surfacePixels_.resize(static_cast<size_t>(DESIGN_W) * DESIGN_H);
        surfaceColor_ = base;
        cachedSurfaceStyle_ = surfaceStyle_;
        cachedSceneChrome_ = sceneChrome_;
        const bool pureBlack = (base & 0x00FFFFFFu) == 0;
        const int inkDirection = ((base >> 16) & 255u) + ((base >> 8) & 255u) + (base & 255u) > 460 ? -1 : 1;
        if (pureBlack || (surfaceStyle_ == 7 && !sceneChrome_)) {
            std::fill(surfacePixels_.begin(), surfacePixels_.end(), base);
        } else for (int py = 0; py < DESIGN_H; ++py) {
            const int light = 2 - py * 3 / DESIGN_H;
            for (int px = 0; px < DESIGN_W; ++px) {
                // Legacy textures keep their exact range. New opt-in materials permit a little
                // more relief, still far below the contrast of any foreground data or label.
                const int delta = std::clamp(light + material_delta(surfaceStyle_, px, py), -4, surfaceStyle_>=12?6:4);
                const int texture = sceneChrome_ ? scene_texture(sceneChrome_, px, py) : 0;
                surfacePixels_[static_cast<size_t>(py) * DESIGN_W + px] = shade(base, delta + inkDirection * texture);
            }
        }
    }
    const bool opaque = (base >> 24) == 255;
    for (int py = y0; py < y1; ++py) {
        auto* dest = px_.data() + static_cast<size_t>(py) * DESIGN_W;
        const auto* src = surfacePixels_.data() + static_cast<size_t>(py) * DESIGN_W;
        if (opaque) std::copy(src + x0, src + x1, dest + x0);
        else for (int px = x0; px < x1; ++px) dest[px] = blend(dest[px], src[px]);
    }
    if (sceneChrome_ && w >= 450 && h >= 180) {
        const int index = sceneChrome_ - 1;
        const int variant = scene_look(index).variant;
        const auto family = scene_look(index).family;
        const Argb quiet = (sceneInk_ & 0xffffffu) | 0x16000000u;
        scene_edge(x, y, w, h, quiet);
        // The texture stays behind all the editor content. Motifs use the margins and gutter only.
        constexpr const char* stamps[] = {"#::#", "+-+", "[::]", ">_", "::>", "===", "/!/", "<*>"};
        const char* stamp = stamps[static_cast<int>(family)];
        for (int py = y + 20 + variant * 5; py + 10 < y + h; py += 42 + variant * 4)
            draw_text(stamp, x + w - 23, py, quiet, 1, 1);
    }
}

void Canvas::scene_edge(int x, int y, int w, int h, Argb color) {
    if (!sceneChrome_ || w < 12 || h < 8) return;
    const Absolute at(*this, x);
    const int index = sceneChrome_ - 1;
    const int variant = scene_look(index).variant;
    const auto family = scene_look(index).family;
    const int corner = std::min(5 + variant * 2, w / 3);
    const auto rail = [&](int py, int height = 1) { fill_rect(x + 2, py, w - 4, height, color); };
    const auto corners = [&](int thickness) {
        for (int dx : {0, w - corner}) {
            fill_rect(x + dx, y, corner, thickness, color);
            fill_rect(x + dx, y + h - thickness, corner, thickness, color);
        }
        for (int dx : {0, w - thickness}) {
            fill_rect(x + dx, y, thickness, 4, color);
            fill_rect(x + dx, y + h - 4, thickness, 4, color);
        }
    };
    switch (family) {
        case SceneFamily::UNSCII:
            corners(2);
            for (int dx = corner + 2; dx + corner + 2 < w; dx += 8 + 2 * variant)
                fill_rect(x + dx, y + (dx / 2) % 2, 3, 2, color);
            break;
        case SceneFamily::ASCII:
            corners(1);
            for (int dx = corner + 2; dx + corner + 2 < w; dx += 5 + variant)
                fill_rect(x + dx, y + h - 1, 2, 1, color);
            break;
        case SceneFamily::DOS:
            stroke_rect(x, y, w, h, color);
            rail(y + 2);
            rail(y + h - 3);
            if (variant & 1) fill_rect(x + w - 2, y + 2, 2, h - 4, color);
            break;
        case SceneFamily::TERMINAL:
            fill_rect(x, y, 2, h, color);
            rail(y + h - 1);
            fill_rect(x + w - corner - 2, y + 1, corner, 2, color);
            break;
        case SceneFamily::ANSI:
            stroke_rect(x, y, w, h, color);
            for (int dx = 2; dx + 4 < w; dx += 6 + variant)
                fill_rect(x + dx, y + h - 3, 4, 2, color);
            break;
        case SceneFamily::RASTER:
            rail(y, 2);
            rail(y + 3);
            fill_rect(x + w - corner - 2, y + h - 2, corner, 2, color);
            break;
        case SceneFamily::CRACKTRO:
            corners(2);
            for (int dx = corner + 3; dx + corner + 3 < w; dx += 10 + variant * 2)
                fill_rect(x + dx, y + h - 1 - (dx % 3), 4, 1, color);
            break;
        case SceneFamily::VECTOR:
            corners(1);
            line(x + 1, y + 4, x + 4, y + 1, color);
            line(x + w - 5, y + h - 2, x + w - 2, y + h - 5, color);
            fill_rect(x + w / 2 - variant, y + h - 1, 3 + variant * 2, 1, color);
            break;
    }
}

void Canvas::panel_rect(int x, int y, int w, int h, Argb base, Argb border, int radius) {
    if (w <= 0 || h <= 0) return;
    const Absolute at(*this, x);
    if (sceneChrome_ && w >= 24 && h >= 14) {
        fill_rect(x, y, w, h, base);
        const Argb edge = (border >> 24) < 0x55 ? (border & 0xffffffu) | 0x55000000u : border;
        scene_edge(x, y, w, h, edge);
        return;
    }
    const int r = std::clamp(radius, 0, std::min(w, h) / 2);
    const auto inset = [](int row, int height, int rad) {
        if (rad == 0) return 0;
        const int edge = std::min(row, height - 1 - row);
        if (edge >= rad) return 0;
        const double dy = rad - edge - .5;
        return std::max(0, static_cast<int>(std::ceil(rad - std::sqrt(rad * rad - dy * dy) - .5)));
    };
    for (int row = 0; row < h; ++row) {
        const int outer = inset(row, h, r);
        if (row == 0 || row == h - 1 || w <= 2) {
            fill_rect(x + outer, y + row, w - 2 * outer, 1, border);
            continue;
        }
        const int inner = std::max(outer + 1, 1 + inset(row - 1, h - 2, std::max(0, r - 1)));
        // Border and fill spans never overlap: translucent shadows composite once.
        fill_rect(x + outer, y + row, inner - outer, 1, border);
        fill_rect(x + w - inner, y + row, inner - outer, 1, border);
        const bool opaque = (base >> 24) == 255;
        const int light = opaque ? (row == 1 ? 7 : 2 - row * 3 / h) : 0;
        fill_rect(x + inner, y + row, w - 2 * inner, 1, shade(base, light));
    }
}

void Canvas::stroke_rect(int x, int y, int w, int h, Argb color, int thickness) {
    if (w <= 0 || h <= 0 || thickness <= 0) return;
    const Absolute at(*this, x);
    const int t = std::min(thickness, std::min(w, h));
    fill_rect(x, y, w, t, color);                  // top
    fill_rect(x, y + h - t, w, t, color);          // bottom
    fill_rect(x, y + t, t, h - 2 * t, color);      // left
    fill_rect(x + w - t, y + t, t, h - 2 * t, color);  // right
}

void Canvas::line(int x0, int y0, int x1, int y1, Argb color) {
    const Absolute at(*this, x0, x1);
    if (clipW_ <= 0 || clipH_ <= 0 || (color >> 24) == 0) return;
    const int left = clipX_, right = clipX_ + clipW_ - 1;
    const int top = clipY_, bottom = clipY_ + clipH_ - 1;
    const double dx = static_cast<double>(x1) - x0, dy = static_cast<double>(y1) - y0;
    double enter = 0, leave = 1;
    const auto trim = [&](double p, double q) {
        if (p == 0) return q >= 0;
        const double t = q / p;
        if (p < 0) { if (t > leave) return false; enter = std::max(enter, t); }
        else { if (t < enter) return false; leave = std::min(leave, t); }
        return true;
    };
    if (!trim(-dx, static_cast<double>(x0) - left) || !trim(dx, static_cast<double>(right) - x0) ||
        !trim(-dy, static_cast<double>(y0) - top) || !trim(dy, static_cast<double>(bottom) - y0)) return;
    const int endX = std::clamp(static_cast<int>(std::lround(x0 + leave * dx)), left, right);
    const int endY = std::clamp(static_cast<int>(std::lround(y0 + leave * dy)), top, bottom);
    int x = std::clamp(static_cast<int>(std::lround(x0 + enter * dx)), left, right);
    int y = std::clamp(static_cast<int>(std::lround(y0 + enter * dy)), top, bottom);
    const int spanX = std::abs(endX - x), spanY = -std::abs(endY - y);
    const int stepX = x < endX ? 1 : -1, stepY = y < endY ? 1 : -1;
    int error = spanX + spanY;
    for (;;) {
        auto& pixel = px_[static_cast<size_t>(y) * DESIGN_W + x];
        pixel = blend(pixel, color);
        if (x == endX && y == endY) break;
        const int twice = error * 2;
        if (twice >= spanY) { error += spanY; x += stepX; }
        if (twice <= spanX) { error += spanX; y += stepY; }
    }
}

// Each source pixel a font_scale square. Drawn as runs and bands rather than per pixel (text is most
// of a frame's cost); the rects never overlap, so a translucent colour blends each pixel once.
void Canvas::draw_glyph(const Glyph& g, int x, int y, Argb color, int font_scale) {
    if (font_scale <= 0) return;
    const Absolute at(*this, x);
    for (int row = 0; row < 5;) {
        const uint8_t bits = g[static_cast<size_t>(row)];
        if (bits == 0) { ++row; continue; }

        int last = row + 1;
        while (last < 5 && g[static_cast<size_t>(last)] == bits) ++last;
        const int band_y = y + row * font_scale;
        const int band_h = (last - row) * font_scale;

        for (int col = 0; col < 5;) {
            if (!((bits >> (4 - col)) & 1)) { ++col; continue; }
            int end = col + 1;
            while (end < 5 && ((bits >> (4 - end)) & 1)) ++end;
            fill_rect(x + col * font_scale, band_y, (end - col) * font_scale, band_h, color);
            col = end;
        }
        row = last;
    }
}

namespace {
// The lowest set bit's position (window != 0).
inline int lowest_bit(uint32_t window) {
#if defined(_MSC_VER) && !defined(__clang__)
    unsigned long index;
    _BitScanForward(&index, window);
    return static_cast<int>(index);
#else
    return __builtin_ctz(window);
#endif
}

// One row of a 1-bit glyph: pixels [first, first + count) of a packed face onto dest[0..count),
// each set bit blended as full ink. Mostly empty, so the bits are taken up to 24 at a time and
// runs of clear pixels skipped whole, instead of testing pixel by pixel. Only the bytes holding
// those pixels are read — never past the atlas.
// Kept out of line: inlined, it bloats draw_label/draw_text and slows their byte-atlas loops.
#if defined(_MSC_VER) && !defined(__clang__)
__declspec(noinline)
#else
__attribute__((noinline))
#endif
void blend_bit_row(uint32_t* dest, const uint8_t* bits, uint32_t first, int count, uint32_t ink) {
    int done = 0;
    while (done < count) {
        const uint32_t at = first + static_cast<uint32_t>(done);
        const uint32_t byte = at >> 3, shift = at & 7u;
        const int need = std::min(24, count - done);
        const int bytes = (static_cast<int>(shift) + need + 7) / 8;
        uint32_t window = 0;
        for (int k = 0; k < bytes; ++k) window |= static_cast<uint32_t>(bits[byte + static_cast<uint32_t>(k)]) << (8 * k);
        window = (window >> shift) & ((1u << need) - 1u);
        while (window) {
            const int z = lowest_bit(window);
            dest[done + z] = blend(dest[done + z], ink);
            window &= window - 1u;
        }
        done += need;
    }
}
}  // namespace

void Canvas::draw_text(const std::string& text, int x, int y, Argb color, int spacing,
                       int font_scale) {
    const Absolute at(*this, x);
    int cx = x;
    for (size_t i = 0; i < text.size();) {
        const auto cp=next_codepoint(text,i);
        if((font_scale==2||font_scale==3)&&cp>=32&&cp<127){
            const int cell=font_scale*5;
            const int height=font_scale==3?studioFont15Height:studioFont10Height;
            const detail::DemoAlpha custom=fontStyle_?detail::demo_grid_pixels(fontStyle_,font_scale,cp):detail::DemoAlpha{};
            const auto* g=font_scale==3?studioFont15[cp-32]:studioFont10[cp-32];
            const int left = std::max(0, clipX_ - cx), right = std::min(cell, clipX_ + clipW_ - cx);
            const int top = std::max(0, clipY_ - y), bottom = std::min(height, clipY_ + clipH_ - y);
            const unsigned colorAlpha = color >> 24;
            if (custom) {
                // A 1-bit face: a set pixel is full ink, (255·a + 127) / 255 = a; a clear one is none.
                const Argb ink = color;
                for (int gy = top; gy < bottom; ++gy) {
                    auto* dest = px_.data() + static_cast<size_t>(y + gy) * DESIGN_W + cx + left;
                    blend_bit_row(dest, custom.bits, custom.start + static_cast<uint32_t>(gy * cell + left),
                                  right - left, ink);
                }
            } else {
                for (int gy = top; gy < bottom; ++gy) {
                    auto* dest = px_.data() + static_cast<size_t>(y + gy) * DESIGN_W;
                    for (int gx = left; gx < right; ++gx) {
                        const unsigned alpha = (g[gy * cell + gx] * colorAlpha + 127) / 255;
                        if (alpha) dest[cx + gx] = blend(dest[cx + gx], (color & 0x00ffffffu) | alpha << 24);
                    }
                }
            }
        }else draw_glyph(glyph_for_codepoint(cp),cx,y,color,font_scale);
        cx += 5 * font_scale + spacing;
    }
}

void Canvas::draw_label(const std::string& text, int x, int y, Argb color, int size) {
    if (size <= 0 || clipW_ <= 0 || clipH_ <= 0 || (color >> 24) == 0) return;
    const Absolute at(*this, x);
    auto font = label_font(size);
    const detail::DemoAlpha custom=fontStyle_?detail::demo_label_pixels(fontStyle_,size):detail::DemoAlpha{};
    if (static_cast<int64_t>(y) + font.height <= clipY_ || y >= clipY_ + clipH_) return;
    const int top = static_cast<int>(std::max<int64_t>(0, static_cast<int64_t>(clipY_) - y));
    const int bottom = static_cast<int>(std::min<int64_t>(font.height, static_cast<int64_t>(clipY_) + clipH_ - y));
    const unsigned colorAlpha = color >> 24;
    int64_t pen = x;
    for (size_t i = 0; i < text.size();) {
        if (pen >= clipX_ + clipW_) break;
        const auto cp = next_codepoint(text, i);
        const int index = label_index(cp);
        if (index >= 0) {
            const auto& glyph = font.glyphs[index];
            const int k = font.scale;   // destination pixels per atlas pixel, each way
            const int width = glyph.width * k;
            const int64_t gx = pen + glyph.left * k;
            if (glyph.width > 0 && gx + width > clipX_ && gx < clipX_ + clipW_) {
                const int left = static_cast<int>(std::max<int64_t>(0, static_cast<int64_t>(clipX_) - gx));
                const int right = static_cast<int>(std::min<int64_t>(width, static_cast<int64_t>(clipX_) + clipW_ - gx));
                // Three inner loops, chosen once per glyph: the byte atlas and the 1-bit faces at the
                // ordinary sizes (no scaling arithmetic per pixel), and the doubled title size.
                if (k == 1 && !custom) {
                    for (int row = top; row < bottom; ++row) {
                        auto* dest = px_.data() + static_cast<size_t>(y + row) * DESIGN_W + gx;
                        const auto* source = font.pixels + glyph.offset + row * glyph.width;
                        for (int col = left; col < right; ++col) {
                            const unsigned alpha = (source[col] * colorAlpha + 127) / 255;
                            if (alpha) dest[col] = blend(dest[col], (color & 0x00ffffffu) | alpha << 24);
                        }
                    }
                } else if (k == 1) {
                    // A set bit is full ink: (255·a + 127) / 255 = a.
                    for (int row = top; row < bottom; ++row) {
                        auto* dest = px_.data() + static_cast<size_t>(y + row) * DESIGN_W + gx + left;
                        blend_bit_row(dest, custom.bits,
                                      custom.start + glyph.offset + static_cast<uint32_t>(row * glyph.width + left),
                                      right - left, color);
                    }
                } else {
                    for (int row = top; row < bottom; ++row) {
                        auto* dest = px_.data() + static_cast<size_t>(y + row) * DESIGN_W;
                        const uint32_t first = glyph.offset + static_cast<uint32_t>((row / k) * glyph.width);
                        const auto* source = font.pixels + first;
                        for (int col = left; col < right; ++col) {
                            const int sx = col / k;
                            const unsigned value = custom ? custom.alpha(first + static_cast<uint32_t>(sx)) : source[sx];
                            const unsigned alpha = (value * colorAlpha + 127) / 255;
                            if (alpha) dest[gx + col] = blend(dest[gx + col], (color & 0x00ffffffu) | alpha << 24);
                        }
                    }
                }
            }
            pen += glyph.advance * k;
        } else {
            const int cell = font.fallbackScale * 5;
            if (pen + cell > clipX_)
                draw_glyph(glyph_for_codepoint(cp), static_cast<int>(pen), y + (font.height - cell) / 2, color, font.fallbackScale);
            pen += cell;
        }
    }
}

int Canvas::label_width(const std::string& text, int size) {
    if (size <= 0) return 0;
    const auto font = label_font(size);
    int64_t width = 0;
    for (size_t i = 0; i < text.size();) {
        width += label_advance(next_codepoint(text, i), font);
        if (width >= std::numeric_limits<int>::max()) return std::numeric_limits<int>::max();
    }
    return static_cast<int>(width);
}

std::string Canvas::clip_label(const std::string& text, int maxWidth, int size) {
    if (maxWidth <= 0 || size <= 0) return {};
    if (label_width(text, size) <= maxWidth) return text;
    const auto font = label_font(size);
    const int marker = label_advance(0x2026, font);
    if (marker > maxWidth) return {};
    int width = 0;
    size_t keep = 0;
    for (size_t i = 0; i < text.size();) {
        const int advance = label_advance(next_codepoint(text, i), font);
        if (advance > maxWidth - marker - width) break;
        width += advance;
        keep = i;
    }
    return text.substr(0, keep) + "\xE2\x80\xA6";
}

int Canvas::text_width(const std::string& text, int spacing, int font_scale) {
    return glyph_count(text) * (5 * font_scale + spacing) - (text.empty() ? 0 : spacing);
}

int Canvas::glyph_count(const std::string& text) {
    int n = 0;
    for (size_t i = 0; i < text.size();) {
        next_codepoint(text, i);
        ++n;
    }
    return n;
}

std::string Canvas::clip_text(const std::string& text, int max_glyphs) {
    if (max_glyphs <= 0) return {};

    size_t i   = 0;
    size_t cut = 0;   // byte offset just past glyph `max_glyphs - 1`, where the marker goes
    int    n   = 0;

    while (i < text.size()) {
        if (n == max_glyphs - 1) cut = i;
        next_codepoint(text, i);
        if (++n > max_glyphs) return text.substr(0, cut) + "\xE2\x80\xA6";
    }
    return text;
}

std::string Canvas::clip_text_head(const std::string& text, int max_glyphs) {
    if (max_glyphs <= 0) return {};
    const int total = glyph_count(text);
    if (total <= max_glyphs) return text;

    size_t i = 0;   // from the front: only that lands on a code-point boundary
    for (int skip = total - (max_glyphs - 1); skip > 0; --skip) next_codepoint(text, i);
    return "\xE2\x80\xA6" + text.substr(i);
}

}  // namespace pt::ui
