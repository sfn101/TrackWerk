#include "sdl/sdl-touch.h"

#include "sdl/sdl-input.h"

#include "ui/canvas.h"           // DESIGN_W: 640, or 854 in wide mode
#include "skin/button_glyphs.h"  // shared with the portrait cluster

#include <algorithm>
#include <cstdio>

namespace tl = pt::ui::touch_layout;

namespace {

// Narrower bars are not finger-sized.
constexpr int MIN_PANEL_PX = 150;

constexpr uint32_t BTN_NORMAL  = 0x3D5A80;
constexpr uint32_t BTN_PRESSED = 0x98C1D9;
constexpr uint32_t PANEL_BG    = 0x1A1A1A;

void fill(SDL_Renderer* r, const SDL_Rect& rc, uint32_t rgb, Uint8 alpha = 255) {
    SDL_SetRenderDrawBlendMode(r, alpha == 255 ? SDL_BLENDMODE_NONE : SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, static_cast<Uint8>((rgb >> 16) & 0xFF),
                           static_cast<Uint8>((rgb >> 8) & 0xFF), static_cast<Uint8>(rgb & 0xFF), alpha);
    SDL_RenderFillRect(r, &rc);
}

// Wide mode's overlay buttons: about 40 % see-through, solid while pressed.
constexpr Uint8 OVERLAY_ALPHA = 153;

}  // namespace

void SdlTouch::layout(const SDL_Rect& frame, int outW, int outH) {
    outW_ = outW;
    outH_ = outH;
    portrait_ = false;
    frame_ = frame;

    // The bars either side of the centred frame; none wide enough → inactive. Overlaid (wide mode),
    // each box is a fifth of the width at the picture's edge, whatever bars there are.
    const int overlayW = std::max(MIN_PANEL_PX, outW / 5);
    const int leftW  = overlay_ ? overlayW : frame.x;
    const int rightX = overlay_ ? outW - overlayW : frame.x + frame.w;
    const int rightW = outW - rightX;

    active_ = enabled_ && outH > 0 && leftW >= MIN_PANEL_PX && rightW >= MIN_PANEL_PX;
    if (!active_) {
        left_.count  = 0;
        right_.count = 0;
        return;
    }

    leftBox_  = {0, 0, leftW, outH};
    rightBox_ = {rightX, 0, rightW, outH};
    left_     = tl::left_rects(leftW, outH);
    right_    = tl::right_rects(rightW, outH);
}

void SdlTouch::layout_portrait2(const SDL_Rect& cluster, const tl::BoxRects& rects, int outW, int outH) {
    outW_            = outW;
    outH_            = outH;
    portrait_        = true;
    portraitCluster_ = cluster;
    portrait_rects_  = rects;
    // PortraitSkin fills rects only on a portrait output, so count 0 means nothing to press.
    active_ = enabled_ && rects.count > 0;
}

bool SdlTouch::hit_window(int px, int py, Button& out) const {
    // Portrait: one cluster, rects local to its origin.
    if (portrait_) {
        return tl::hit(portrait_rects_, px - portraitCluster_.x, py - portraitCluster_.y, out);
    }
    if (px >= leftBox_.x && px < leftBox_.x + leftBox_.w) {
        return tl::hit(left_, px - leftBox_.x, py - leftBox_.y, out);
    }
    if (px >= rightBox_.x && px < rightBox_.x + rightBox_.w) {
        return tl::hit(right_, px - rightBox_.x, py - rightBox_.y, out);
    }
    return false;
}

bool SdlTouch::canvas_point(const SDL_Event& e, int& canvasX, int& canvasY) const {
    if (portrait_ || frame_.w <= 0 || frame_.h <= 0 || outW_ <= 0 || outH_ <= 0) return false;
    const int px = static_cast<int>(e.tfinger.x * outW_), py = static_cast<int>(e.tfinger.y * outH_);
    if (px < frame_.x || py < frame_.y || px >= frame_.x + frame_.w || py >= frame_.y + frame_.h) return false;
    canvasX = (px - frame_.x) * pt::ui::DESIGN_W / frame_.w;
    canvasY = (py - frame_.y) * pt::ui::DESIGN_H / frame_.h;
    return true;
}

bool SdlTouch::handle_finger(const SDL_Event& e, SdlInput& input, uint64_t now_ms) {
    if (!active_) return false;

    // SDL normalises fingers to [0,1] over the window — the output's pixel space on Android.
    const SDL_TouchFingerEvent& t  = e.tfinger;
    const int                   px = static_cast<int>(t.x * outW_);
    const int                   py = static_cast<int>(t.y * outH_);
    bool                        consumed = false;

    if (e.type == SDL_FINGERDOWN) {
        Button b{};
        if (hit_window(px, py, b)) {
            consumed = true;
            input.touch_press(b, now_ms);
            emit_feedback(b, /*down=*/true);
            finger_[t.fingerId] = b;
            if (trace_) std::printf("input:   TOUCH DOWN  %4d,%-4d       -> %s\n", px, py, pt::ui::button_name(b));
        } else if (trace_) {
            // ⚠️ A tap that hits nothing says so: a gap, vs no event at all.
            std::printf("input:   TOUCH DOWN  %4d,%-4d       -> (no button)\n", px, py);
        }
    } else if (e.type == SDL_FINGERUP) {
        auto it = finger_.find(t.fingerId);
        if (it != finger_.end()) {
            consumed = true;
            Button button{};
            const bool inside = hit_window(px, py, button) && button == it->second;
            input.touch_release(it->second, !inside);
            emit_feedback(it->second, /*down=*/false);
            if (trace_) std::printf("input:   TOUCH UP                    -> %s\n", pt::ui::button_name(it->second));
            finger_.erase(it);
        }
    } else if (e.type == SDL_FINGERMOTION) {
        // Sliding off releases; sliding back in does not re-press.
        auto it = finger_.find(t.fingerId);
        if (it != finger_.end()) {
            consumed = true;
            Button b{};
            const bool still = hit_window(px, py, b) && b == it->second;
            if (!still) {
                input.touch_release(it->second, true);
                emit_feedback(it->second, /*down=*/false);
                if (trace_)
                    std::printf("input:   TOUCH SLIDE-OFF             -> %s released\n",
                                pt::ui::button_name(it->second));
                finger_.erase(it);
            }
        }
    }
    return consumed;
}

void SdlTouch::draw(SDL_Renderer* r, const SdlInput& input) const {
    if (!active_ || portrait_) return;   // the portrait cluster is PortraitSkin's

    if (!overlay_) {   // overlaid, the picture shows between the buttons
        fill(r, leftBox_, PANEL_BG);
        fill(r, rightBox_, PANEL_BG);
    }

    auto draw_box = [&](const tl::BoxRects& box, int ox, int oy) {
        for (int i = 0; i < box.count; ++i) {
            const tl::ButtonRect& br = box.r[i];
            const SDL_Rect        rc{br.x + ox, br.y + oy, br.w, br.h};
            const bool held = input.is_held(br.button);
            fill(r, rc, held ? BTN_PRESSED : BTN_NORMAL, overlay_ && !held ? OVERLAY_ALPHA : 255);
            ptshell::draw_label(r, br.button, rc);
        }
    };
    draw_box(left_, leftBox_.x, leftBox_.y);
    draw_box(right_, rightBox_.x, rightBox_.y);
}

uint64_t SdlTouch::signature(const SdlInput& input) const {
    if (!active_) return 0;

    uint64_t bits = 0;
    for (int i = 0; i < left_.count; ++i)
        if (input.is_held(left_.r[i].button)) bits |= (1ull << static_cast<int>(left_.r[i].button));
    for (int i = 0; i < right_.count; ++i)
        if (input.is_held(right_.r[i].button)) bits |= (1ull << static_cast<int>(right_.r[i].button));

    // Geometry too, so a resize repaints; bit 63 keeps it non-zero while active.
    return bits | (static_cast<uint64_t>(outW_ & 0xFFFF) << 16) |
           (static_cast<uint64_t>(outH_ & 0xFFFF) << 32) | (1ull << 63);
}
