#pragma once

// SETTINGS > SCALING's size maths (specs/sharp-scaling.md), with no SDL in it so a host check can
// test it. The SDL shell (`shell/sdl/sdl-video.cpp`) draws with these numbers.

#include <algorithm>

namespace pt::ui {

// The largest srcW:srcH rectangle that fits outW×outH (FIT and SHARP), at least 1×1.
// ⚠️ Integer arithmetic: a float scale can land at 959.99997 and leave a 1 px bar on an exact multiple.
inline void fit_size(int srcW, int srcH, int outW, int outH, int& w, int& h) {
    if (outW * srcH >= outH * srcW) {   // wider than the picture: the height binds
        h = outH;
        w = outH * srcW / srcH;
    } else {                            // taller: the width binds
        w = outW;
        h = outW * srcH / srcW;
    }
    w = std::max(1, w);
    h = std::max(1, h);
}

// SHARP's whole-number enlargement: the smallest N that makes N·src at least destW×destH, so the
// final shrink only ever filters down. Capped so N·src fits a maxTexW×maxTexH texture; at least 1.
inline int sharp_factor(int srcW, int srcH, int destW, int destH, int maxTexW, int maxTexH) {
    const int need = std::max((destW + srcW - 1) / srcW, (destH + srcH - 1) / srcH);
    const int cap  = std::min(maxTexW / srcW, maxTexH / srcH);
    return std::max(1, std::min(need, cap));
}

}  // namespace pt::ui
