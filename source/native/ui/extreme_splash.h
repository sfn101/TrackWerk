// TrackWerk's deterministic, silent demoscene introduction.
// Draw-only: timing, skip input and startup ownership remain with the shell.
#pragma once
#include "ui/demo_fx.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include "canvas.h"
#include "trackwerk_art.h"

namespace pt::ui {
namespace extreme_art {
constexpr float pi = 3.14159265358979323846f;
inline float fract(float x) { return x - std::floor(x); }
inline Argb alpha(Argb color, float a) {
    return (color & 0x00ffffffu) | (Argb(std::clamp(a, 0.f, 1.f) * 255.f) << 24);
}
inline Argb mix(Argb a, Argb b, float t) {
    t = std::clamp(t, 0.f, 1.f);
    auto channel = [&](int shift) {
        return Argb(float((a >> shift) & 255) * (1.f - t) + float((b >> shift) & 255) * t);
    };
    return 0xff000000u | channel(16) << 16 | channel(8) << 8 | channel(0);
}
inline uint32_t hash(uint32_t value) {
    value ^= value >> 16; value *= 0x7feb352du;
    value ^= value >> 15; value *= 0x846ca68bu;
    return value ^ (value >> 16);
}
struct Point { int x, y; float depth; };

// TrackWerk's original nine-letter mark, drawn from the same master as the vector study.
inline void logo(Canvas& c, int x, int y, Argb accent, Argb violet, float progress) {
    constexpr int cell=6, advance=50;
    const float reveal=.45f+.55f*std::min(1.f,progress*7.f);
    for(int row=0;row<9;++row){
        const Argb ink=row<3?accent:row<5?0xffedf6ffu:violet;
        const int settle=progress<.2f?int(std::sin(row*2.1f+progress*37.f)*(1.f-progress*5.f)*3.f):0;
        const int skew=8-row+settle;
        for(int letter=0;letter<9;++letter){
            const uint8_t bits=brand::art::wordmarkRows[letter][row];
            for(int col=0;col<7;){
                if(!(bits&(64>>col))){++col;continue;}
                int end=col+1;while(end<7&&(bits&(64>>end)))++end;
                const int left=x+letter*advance+col*cell+skew,top=y+row*cell,width=(end-col)*cell;
                c.fill_rect(left+4,top+5,width,cell,alpha(violet,.15f*reveal));
                c.fill_rect(left,top,width,cell,alpha(ink,reveal));
                c.fill_rect(left,top,width,1,alpha(0xffffffffu,.16f*reveal));
                col=end;
            }
        }
    }
}
}

inline void draw_extreme_splash(Canvas& c, const Theme& theme, float progress) {
    using namespace extreme_art;
    const float p = std::isfinite(progress) ? std::clamp(progress, 0.f, 1.f) : 0.f;
    const Argb ground = mix(0xff070b16u, theme.background, .07f);
    const Argb mint = mix(0xff65e7d0u, theme.rowCursor, .30f);
    const Argb violet = mix(0xffa28effu, theme.textTitle, .22f);
    const Argb white = 0xffedf6ffu;
    c.reset_clip(); c.set_surface_style(theme.surfaceStyle);
    c.surface_rect(0, 0, DESIGN_W, DESIGN_H, ground);
    // Wide mode: the frame, floor, stars and copper band span the canvas; the globe, logo and words
    // keep their place in the middle. In 4:3 W = 640 and dx = 0, so nothing moves.
    const int W = DESIGN_W, dx = wide_dx(), cx = W / 2;
    const float stretch = float(W) / 640.f;

    // Edge registration and quiet perspective floor. The travelling rings are geometric artwork,
    // not a simulated loading diagnostic or an audio activity meter.
    c.stroke_rect(18, 18, W - 36, 444, alpha(violet, .18f));
    c.fill_rect(18, 18, 74, 1, alpha(mint, .78f));
    c.fill_rect(W - 92, 461, 74, 1, alpha(mint, .58f));
    for (int i = 0; i <= 10; ++i)
        c.line(cx, 196, cx + (i * 96 - 480) * W / 640, 432, alpha(violet, .11f));
    for (int i = 0; i < 10; ++i) {
        const float depth = fract(float(i) / 10.f + p * .35f);
        const int yy = 196 + int(depth * depth * 236.f);
        c.line(20, yy, W - 20, yy, alpha(violet, .035f + depth * .10f));
    }
    for (int i = 0; i < 56; ++i) {
        const uint32_t seed = hash(uint32_t(i) + 63u);
        const float angle = float(seed & 65535u) / 65535.f * 2.f * pi;
        const float depth = .16f + .84f * fract(float(seed >> 16) / 65535.f + p * .22f);
        const float radius = 25.f + 156.f * depth;
        const int sx = cx + int(std::cos(angle) * radius * 1.58f * stretch);
        const int sy = 139 + int(std::sin(angle) * radius * .60f);
        const Argb ink = alpha(i % 3 == 0 ? violet : mint, .10f + depth * .45f);
        const int length = depth > .8f ? 3 : 1;
        c.line(sx, sy, sx + int(std::cos(angle) * length), sy + int(std::sin(angle) * length), ink);
        if (i % 11 == 0) c.fill_rect(sx, sy, 2, 2, alpha(white, .6f));
    }

    // Rotating latitude/longitude shell with a shallow, audio-like surface deformation.
    const float yaw = p * 1.65f + .3f, pitch = .22f + .10f * std::sin(p * 4.f);
    const float cy = std::cos(yaw), sy = std::sin(yaw), cp = std::cos(pitch), sp = std::sin(pitch);
    const auto project = [&](float latitude, float longitude) {
        const float wave = 1.f + .035f * std::sin(longitude * 5.f + latitude * 3.f + p * 8.f);
        const float ux = std::cos(latitude) * std::cos(longitude) * wave;
        const float uy = std::sin(latitude) * wave;
        const float uz = std::cos(latitude) * std::sin(longitude) * wave;
        const float rx = ux * cy + uz * sy, rz = -ux * sy + uz * cy;
        const float ry = uy * cp - rz * sp, z = uy * sp + rz * cp;
        const float perspective = 3.2f / (3.2f + z);
        return Point{cx + int(rx * 77.f * perspective), 139 + int(ry * 77.f * perspective), z};
    };
    for (int lat = -2; lat <= 2; ++lat) {
        const float latitude = float(lat) * pi / 7.f;
        Point previous = project(latitude, 0);
        for (int segment = 1; segment <= 20; ++segment) {
            const auto q = project(latitude, float(segment) * 2.f * pi / 20.f);
            const float front = std::clamp(.5f - (previous.depth + q.depth) * .32f, 0.f, 1.f);
            c.line(previous.x, previous.y, q.x, q.y, alpha(mix(violet, mint, front), .14f + front * .54f));
            previous = q;
        }
    }
    for (int longitude = 0; longitude < 6; ++longitude) {
        const float angle = float(longitude) * 2.f * pi / 6.f;
        Point previous = project(-pi * .5f, angle);
        for (int segment = 1; segment <= 16; ++segment) {
            const auto q = project(-pi * .5f + float(segment) * pi / 16.f, angle);
            const float front = std::clamp(.5f - q.depth * .65f, 0.f, 1.f);
            c.line(previous.x, previous.y, q.x, q.y, alpha(mix(violet, mint, front), .12f + front * .48f));
            previous = q;
        }
    }
    for (int segment = 0; segment < 32; ++segment) {
        const float a = float(segment) * 2.f * pi / 32.f, b = float(segment + 1) * 2.f * pi / 32.f;
        const auto orbit = [&](float v) { return Point{cx + int(std::cos(v) * 111.f), 144 + int(std::sin(v) * 22.f + std::cos(v) * 16.f), 0}; };
        const auto one = orbit(a), two = orbit(b);
        c.line(one.x, one.y, two.x, two.y, alpha(violet, segment < 16 ? .24f : .46f));
    }
    const float satellite = p * 4.8f + .9f;
    const int satX = cx + int(std::cos(satellite) * 111.f), satY = 144 + int(std::sin(satellite) * 22.f + std::cos(satellite) * 16.f);
    c.fill_rect(satX - 3, satY - 3, 7, 7, alpha(mint, .09f));
    c.fill_rect(satX - 1, satY - 1, 3, 3, white);

    static const std::string header = "DEMOSCENE MUSIC SYSTEM";
    static const std::string title = "KID BUNKA PRESENTS";
    static const std::string identity = "SYNTHESIS  /  SAMPLES  /  SEQUENCES";
    static const std::string skip = "PRESS ANY KEY TO SKIP";
    c.draw_label(header, (DESIGN_W - Canvas::label_width(header, 12)) / 2, 31, alpha(violet, .78f), 12);
    c.draw_label(title, (DESIGN_W - Canvas::label_width(title, 28)) / 2, 228, white, 28);   // the title size
    // Keep the main mark clean against the floor while allowing the grid to show through its gaps.
    c.fill_rect(94 + dx, 260, 452, 83, alpha(ground, .53f));
    logo(c, 94 + dx, 267, mint, violet, p);
    c.draw_label(identity, (DESIGN_W - Canvas::label_width(identity, 12)) / 2, 355, alpha(white, .66f), 12);

    // Copper bars behind the ribbon, in the splash's own colours: the cracktro's raster band.
    {
        Theme copperTheme = theme;
        copperTheme.background = ground; copperTheme.rowEvery4th = mix(ground, violet, .45f);
        copperTheme.rowCursor = mint; copperTheme.textValue = white;
        Canvas::ClipScope band(c, 20, 372, W - 40, 44);
        demo_fx::copper(c, 20, 372, W - 40, 44, copperTheme, uint64_t(p * 5200.f));
        c.fill_rect(20, 372, W - 40, 44, alpha(ground, .45f));
    }

    // A compact animated ribbon closes the artwork. Its waveform is explicitly decorative.
    int previousX = 160 + dx, previousY = 394;
    for (int segment = 1; segment <= 64; ++segment) {
        const float u = float(segment) / 64.f;
        const int xx = 160 + dx + segment * 5;
        const float envelope = std::sin(u * pi);
        const int yy = 394 + int(envelope * (std::sin(u * 24.f - p * 8.f) * 5.f + std::sin(u * 45.f + p * 7.f) * 3.f));
        c.line(previousX, previousY, xx, yy, alpha(mix(mint, violet, u), .55f));
        previousX = xx; previousY = yy;
    }
    c.fill_rect(240 + dx, 418, 160, 1, alpha(violet, .18f));
    c.fill_rect(240 + dx, 418, std::max(1, int(160.f * p)), 1, alpha(mint, .72f));
    c.draw_label(skip, (DESIGN_W - Canvas::label_width(skip, 12)) / 2, 434, alpha(violet, .58f), 12);

    // Scanlines over everything, and at the very end a tube switching over: the picture folds to a
    // bright line in the middle, and the tracker appears behind it.
    for (int py = 1; py < DESIGN_H; py += 2) c.fill_rect(0, py, DESIGN_W, 1, 0x1c000000u);
    if (p > .9f) {
        const float fold = (p - .9f) / .1f;                       // 0 … 1
        const int open = int((1.f - fold * fold) * DESIGN_H / 2);  // half height still visible
        c.fill_rect(0, 0, DESIGN_W, DESIGN_H / 2 - open, 0xff000000u);
        c.fill_rect(0, DESIGN_H / 2 + open, DESIGN_W, DESIGN_H / 2 - open, 0xff000000u);
        // A thin bright line with a soft halo, brighter as it closes.
        c.fill_rect(0, DESIGN_H / 2 - 6, DESIGN_W, 12, alpha(mint, .10f + .15f * fold));
        c.fill_rect(0, DESIGN_H / 2 - 2, DESIGN_W, 4, alpha(white, .30f + .40f * fold));
        c.fill_rect(0, DESIGN_H / 2 - 1, DESIGN_W, 2, alpha(white, .70f + .30f * fold));
    }
}
}
