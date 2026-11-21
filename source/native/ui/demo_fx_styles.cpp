// The fourteen visualizer styles after PLASMA (specs/more-visualizers.md): classic demoscene effects
// written for TrackWerk's software canvas. Eight are flat (TUNNEL … ROTOZOOM), six are light 3D
// wireframes and dots (CUBE … RIDGES). All draw in the theme's own colours through ramp() and the
// theme's accents, inside the box they are given (the caller clips), with no allocation.
//
// Cheap on purpose, as demo_fx.h says: per-pixel effects work in 4-6 px cells, 3D ones in a few
// hundred lines, and FIRE and RIDGES keep their few kilobytes of state in an EffectMemory.
#include "ui/demo_fx.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "ui/canvas.h"
#include "ui/font5x5.h"
#include "ui/helpers.h"

namespace pt::ui::demo_fx {
namespace {

constexpr float kPi = 3.14159265f;
constexpr int kWave = 620;   // ui/modules/oscilloscope.h WAVEFORM_SIZE

uint32_t mix32(uint32_t n) {
    n ^= n >> 16; n *= 0x7feb352dU; n ^= n >> 15; n *= 0x846ca68bU; n ^= n >> 16;
    return n;
}

// The waveform at 0…1 along it; 0 in silence (a null waveform).
float wave_at(const float* wave, float along) {
    if (!wave) return 0.f;
    const int i = std::clamp(int(along * (kWave - 1)), 0, kWave - 1);
    return std::clamp(wave[i] * 2.f, -1.f, 1.f);
}

// Per-pixel effects use bigger cells on a big box, so VI costs about what the strip does per cell.
int cell_for(int w, int h) { return w * h > 60000 ? 6 : 4; }

// Short, wide boxes (the scope strip) get a row of small copies of a 3D shape instead of one tiny one.
int copies_for(int w, int h) { return std::clamp(w / std::max(1, h * 2), 1, 6); }

struct V3 { float x, y, z; };

V3 rotate(V3 v, float ax, float ay, float az) {
    const float cx = std::cos(ax), sx = std::sin(ax), cy = std::cos(ay), sy = std::sin(ay);
    const float cz = std::cos(az), sz = std::sin(az);
    float y = v.y * cx - v.z * sx, z = v.y * sx + v.z * cx;
    float x = v.x * cy + z * sy;
    z = -v.x * sy + z * cy;
    const float x2 = x * cz - y * sz, y2 = x * sz + y * cz;
    return {x2, y2, z};
}

// Screen point of `v` (unit-sized model) around (cx, cy), `scale` pixels per unit; z from −1 (near)
// to +1 (far) comes back in `depth`.
struct P2 { int x, y; float depth; };
P2 project(V3 v, int cx, int cy, float scale) {
    const float k = 3.2f / (3.2f + v.z);
    return {cx + int(v.x * scale * k), cy + int(v.y * scale * k), v.z};
}

// A depth-shaded line: near is bright accent, far fades toward the panel colour.
Argb depth_ink(const Theme& t, float depth, Argb accent) {
    const int near = std::clamp(int((1.f - depth) * 0.5f * 100.f), 0, 100);
    return mix_rgb(ramp(t, 110), accent, near);
}

void line(Canvas& c, P2 a, P2 b, Argb ink) { c.line(a.x, a.y, b.x, b.y, ink); }

// ── Following the song ──────────────────────────────────────────────────────────────────────────
// The loudness of one frame is noisy, and time × loudness jumps back and forth as it changes, which
// reads as shaking. So the styles get: `clock`, ms that run faster when it is loud (0.6× quiet, 2×
// loud) and only ever move forward; `level`, the loudness smoothed (quick up, slow down); and `kick`,
// which jumps on a hit (the loudness rising above its recent average) and fades in about a quarter
// second. All three hold still with the animation clock while stopped.
struct Music { float clock, level, kick; };

Music listen(EffectMemory& m, uint64_t ms, float e) {
    if (ms != m.pulseMs) {
        const float dt = m.pulseMs == 0 || ms < m.pulseMs ? 16.f : std::min(100.f, float(ms - m.pulseMs));
        m.pulseMs = ms;
        m.level += (e - m.level) * std::min(1.f, dt / (e > m.level ? 50.f : 350.f));
        m.slowLevel += (e - m.slowLevel) * std::min(1.f, dt / 600.f);
        m.kick = std::max(m.kick * std::exp(-dt / 180.f), std::min(1.f, (m.level - m.slowLevel) * 3.f));
        m.musicClock += dt * (.6f + 1.4f * m.level);
    }
    return {m.musicClock, m.level, m.kick};
}

// ── 2D ──────────────────────────────────────────────────────────────────────────────────────────

// Flying down an endless chequered tube: the speed follows the song, the tube sways on a slow curve,
// and a hit pulls the walls in and lights them for a moment.
void tunnel(Canvas& c, int x, int y, int w, int h, const Theme& t, const Music& mu) {
    const int cell = cell_for(w, h);
    const float reach = std::max(1.f, std::min(w, h) * .65f);
    const float spin = mu.clock * .00012f, travel = mu.clock * .04f;
    const float swayX = std::sin(mu.clock * .00045f) * w * .12f, swayY = std::cos(mu.clock * .00031f) * h * .12f;
    const float squeeze = 1.f + .18f * mu.kick;
    for (int py = 0; py < h; py += cell)
        for (int px = 0; px < w; px += cell) {
            const float dx = px + cell * .5f - w * .5f, dy = (py + cell * .5f - h * .5f) * 1.6f;
            const float dist = std::sqrt(dx * dx + dy * dy) + 1.f;
            // The far end drifts with the curve; near the edge the tube is straight.
            const float bend = std::max(0.f, 1.f - dist / reach);
            const float bx = dx - swayX * bend, by = dy - swayY * bend * 1.6f;
            const float d2 = (std::sqrt(bx * bx + by * by) + 1.f) * squeeze;
            const int u = int((std::atan2(by, bx) / kPi + 1.f) * 64.f + spin * 64.f);
            const int v = int(2400.f / d2 + travel);
            const int depth = std::clamp(int(d2 / reach * 255.f), 0, 255);
            const int pattern = ((u >> 3) ^ (v >> 3)) & 1;
            const int shade = (pattern ? 200 : 80) * depth / 255 + int(mu.kick * 45.f * depth / 255);
            c.fill_rect(x + px, y + py, std::min(cell, w - px), std::min(cell, h - py), ramp(t, std::min(255, shade)));
        }
}

// Classic fire, stepped at a calm 30 steps a second whatever the frame rate; louder music, taller
// flames, and a hit throws a burst of sparks.
void fire(Canvas& c, int x, int y, int w, int h, const Theme& t, uint64_t ms, const Music& mu, EffectMemory& m) {
    const int cell = 4;
    const int cols = std::min(w / cell + 1, EffectMemory::kFireCols);
    const int rows = std::min(h / cell + 1, EffectMemory::kFireRows);
    if (cols != m.fireCols || rows != m.fireRows) {
        std::fill(std::begin(m.fire), std::end(m.fire), uint8_t(0));
        m.fireCols = cols;
        m.fireRows = rows;
    }
    const auto at = [&](int r, int col) -> uint8_t& { return m.fire[r * EffectMemory::kFireCols + col]; };
    if (ms != m.fireMs) {   // the clock holds still while stopped, and so do the flames
        const float dt = m.fireMs == 0 || ms < m.fireMs ? 33.f : std::min(200.f, float(ms - m.fireMs));
        m.fireMs = ms;
        m.fireAcc += dt;
        for (int steps = 0; m.fireAcc >= 33.f && steps < 3; ++steps) {
            m.fireAcc -= 33.f;
            const uint32_t tick = ++m.fireTick;
            const int heat = 170 + int(mu.level * 60.f + mu.kick * 25.f);
            for (int col = 0; col < cols; ++col)
                at(rows - 1, col) = (mix32(tick * 7919U + uint32_t(col)) & 255) > 90 ? uint8_t(std::min(255, heat)) : 0;
            // Each cell cools a little as it rises; louder music, taller flames.
            const int cool = std::max(1, int(170.f * (1.3f - .7f * mu.level)) / rows);
            for (int r = 0; r < rows - 1; ++r)
                for (int col = 0; col < cols; ++col) {
                    const int below = at(r + 1, col), left = at(r + 1, std::max(0, col - 1));
                    const int right = at(r + 1, std::min(cols - 1, col + 1));
                    const int far = at(std::min(rows - 1, r + 2), col);
                    at(r, col) = uint8_t(std::max(0, (below + left + right + far) / 4 - cool));
                }
        }
        m.fireAcc = std::min(m.fireAcc, 33.f);
    }
    for (int r = 0; r < rows; ++r)
        for (int col = 0; col < cols; ++col) {
            const int v = at(r, col);
            if (v < 6) continue;
            const Argb ink = v > 200 ? mix_rgb(t.rowCursor, t.textValue, (v - 200) * 100 / 55) : ramp(t, v);
            c.fill_rect(x + col * cell, y + r * cell, cell, cell, ink);
        }
}

// Falling code in small glyphs, dense columns at their own calm speeds; the song speeds the fall.
void rain(Canvas& c, int x, int y, int w, int h, const Theme& t, uint64_t ms, const Music& mu) {
    static constexpr char kGlyphs[] = "0123456789ABCDEF<>+-=*#:";
    constexpr int stepX = 7, stepY = 8;
    const int rows = h / stepY + 1;
    for (int col = 0; col * stepX < w; ++col) {
        const uint32_t r = mix32(uint32_t(col) * 2654435761U + 5U);
        const int trail = 6 + int(r % 12);
        const float speed = 3.f + float((r >> 4) % 6);   // rows a second at the quiet pace
        const int head = int(mu.clock * speed / 1000.f + float((r >> 9) % 97)) % (rows + trail);
        for (int k = 0; k < trail; ++k) {
            const int row = head - k;
            if (row < 0 || row >= rows) continue;
            const uint32_t pick = mix32(uint32_t(col * 131 + row * 17) + (k == 0 ? uint32_t(ms / 150) : 0U));
            const char ch = kGlyphs[pick % (sizeof(kGlyphs) - 1)];
            const Argb ink = k == 0 ? t.textValue : mix_rgb(ramp(t, 50), t.vizWave, 100 - k * 100 / trail);
            c.draw_glyph(glyph_for_codepoint(uint32_t(ch)), x + col * stepX + 1, y + row * stepY, ink, 1);
        }
    }
}

void twister(Canvas& c, int x, int y, int w, int h, const Theme& t, const Music& mu) {
    const int count = std::clamp(w / 280, 1, 3);
    const float radius = std::max(6.f, std::min(float(h) * .45f, float(w) / float(count * 4))) * (1.f + .15f * mu.kick);
    const Argb faces[4] = {ramp(t, 170), t.vizWave, ramp(t, 230), t.rowSelection};
    for (int i = 0; i < count; ++i) {
        const int cx = x + (2 * i + 1) * w / (2 * count);
        for (int py = 0; py < h; py += 2) {
            const float a = std::sin(float(py) * .018f + mu.clock * .0012f + float(i) * 1.7f) * (1.2f + mu.level) +
                            mu.clock * .0009f;
            int xs[5];
            for (int k = 0; k < 5; ++k) xs[k] = cx + int(std::sin(a + float(k) * kPi * .5f) * radius);
            for (int k = 0; k < 4; ++k) {
                if (xs[k + 1] <= xs[k]) continue;   // the face turned away
                const int lit = (xs[k + 1] - xs[k]) * 100 / std::max(1, int(radius * 1.42f));
                c.fill_rect(xs[k], y + py, xs[k + 1] - xs[k], 2, mix_rgb(t.background, faces[k], std::min(100, 35 + lit)));
            }
        }
    }
}

void rings(Canvas& c, int x, int y, int w, int h, const Theme& t, const float* wave, const Music& mu) {
    const int cx = x + w / 2, cy = y + h / 2;
    const float squash = std::min(1.f, float(h) / float(w) * 1.8f);
    const float reach = std::sqrt(float(w * w) * .25f + float(h * h) * .25f / (squash * squash));
    constexpr int kRings = 9, kSides = 48;
    for (int k = 0; k < kRings; ++k) {
        const float r = std::fmod(mu.clock * .04f + float(k) * reach / kRings, reach);
        const Argb ink = ramp(t, std::min(255, 250 - int(r / reach * 190.f) + int(mu.kick * 40.f)));
        int px = cx + int(r), py = cy;
        for (int s = 1; s <= kSides; ++s) {
            const float a = float(s) * 2.f * kPi / kSides;
            const int nx = cx + int(std::cos(a) * r), ny = cy + int(std::sin(a) * r * squash);
            c.line(px, py, nx, ny, ink);
            px = nx; py = ny;
        }
    }
    // The waveform bends the inner ring; a hit swells it.
    const float base = std::min(float(w), float(h) / squash) * (.2f + .06f * mu.kick);
    int px = 0, py = 0;
    for (int s = 0; s <= 96; ++s) {
        const float a = float(s % 96) * 2.f * kPi / 96.f;
        const float r = base * (1.f + .55f * wave_at(wave, float(s % 96) / 96.f));
        const int nx = cx + int(std::cos(a) * r), ny = cy + int(std::sin(a) * r * squash);
        if (s) c.line(px, py, nx, ny, t.vizWave);
        px = nx; py = ny;
    }
}

void lissajous(Canvas& c, int x, int y, int w, int h, const Theme& t, const float* wave, const Music& mu) {
    const int cx = x + w / 2, cy = y + h / 2;
    const float sx = w * .44f, sy = h * .42f;
    const float pa = mu.clock * .0007f, pb = mu.clock * .0011f;
    for (int pass = 1; pass >= 0; --pass) {   // a dim echo a little behind, then the figure
        const float lag = pass * .25f;
        int px = 0, py = 0;
        for (int i = 0; i <= 360; ++i) {
            const float p = float(i) * 2.f * kPi / 360.f;
            const float along = float(i % 360) / 360.f;
            const float fx = std::sin(3.f * p + pa - lag) + .35f * wave_at(wave, along) * (.3f + mu.level);
            const float fy = std::sin(2.f * p + pb - lag) + .35f * wave_at(wave, std::fmod(along + .25f, 1.f)) * (.3f + mu.level);
            const int nx = cx + int(fx / 1.35f * sx), ny = cy + int(fy / 1.35f * sy);
            const Argb ink = pass ? ramp(t, 100) : mix_rgb(t.vizWave, t.rowCursor, int(along * 100.f));
            if (i) c.line(px, py, nx, ny, ink);
            px = nx; py = ny;
        }
    }
}

void blobs(Canvas& c, int x, int y, int w, int h, const Theme& t, const Music& mu) {
    const int cell = cell_for(w, h);
    constexpr int kBlobs = 5;
    float bx[kBlobs], by[kBlobs], rr[kBlobs];
    const float size = float(std::min(w, h)) * (.2f + .06f * mu.level + .06f * mu.kick);
    for (int k = 0; k < kBlobs; ++k) {
        bx[k] = w * (.5f + .38f * std::sin(mu.clock * (.0004f + k * .00013f) + k * 1.3f));
        by[k] = h * (.5f + .36f * std::cos(mu.clock * (.0005f + k * .00011f) + k * 2.1f));
        rr[k] = size * size * (.6f + .1f * k);
    }
    for (int py = 0; py < h; py += cell)
        for (int px = 0; px < w; px += cell) {
            float f = 0.f;
            for (int k = 0; k < kBlobs; ++k) {
                const float dx = px - bx[k], dy = py - by[k];
                f += rr[k] / (dx * dx + dy * dy + 1.f);
            }
            int shade;
            if (f < .7f) shade = int(f * 110.f);
            else if (f < 1.f) shade = 240;                 // the bright rim
            else shade = std::min(255, 140 + int(f * 25.f));
            c.fill_rect(x + px, y + py, std::min(cell, w - px), std::min(cell, h - py),
                        f >= .7f && f < 1.f ? mix_rgb(t.vizWave, t.textValue, 40) : ramp(t, shade));
        }
}

void rotozoom(Canvas& c, int x, int y, int w, int h, const Theme& t, const Music& mu) {
    const int cell = cell_for(w, h);
    const float a = mu.clock * .0004f, zoom = (1.6f + std::sin(mu.clock * .0005f) * .7f) * (1.f - .25f * mu.kick);
    const float ca = std::cos(a) * zoom, sa = std::sin(a) * zoom, drift = mu.clock * .04f;
    for (int py = 0; py < h; py += cell)
        for (int px = 0; px < w; px += cell) {
            const float dx = px - w * .5f, dy = py - h * .5f;
            const int u = int(dx * ca - dy * sa + drift + 4096.f), v = int(dx * sa + dy * ca + 4096.f);
            const int square = ((u >> 4) ^ (v >> 4)) & 1;
            const int shade = (square ? 190 : 70) + ((u & 15) + (v & 15)) * 2;
            c.fill_rect(x + px, y + py, std::min(cell, w - px), std::min(cell, h - py), ramp(t, shade));
        }
}

// ── light 3D ────────────────────────────────────────────────────────────────────────────────────
// They turn on the music clock (faster when loud, never jerking) and punch out on a hit.

void cube(Canvas& c, int x, int y, int w, int h, const Theme& t, const Music& mu) {
    static constexpr int kEdges[12][2] = {{0, 1}, {1, 3}, {3, 2}, {2, 0}, {4, 5}, {5, 7},
                                          {7, 6}, {6, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    const int n = copies_for(w, h);
    // Small enough that a near corner, enlarged by the perspective and a hit, stays in the box.
    const float scale = float(std::min(h, w / n)) * .18f * (1.f + .3f * mu.kick);
    for (int i = 0; i < n; ++i) {
        const int cx = x + (2 * i + 1) * w / (2 * n), cy = y + h / 2;
        for (int shape = 0; shape < 2; ++shape) {   // the outer cube, then an inner one turning the other way
            const float dir = shape ? -1.3f : 1.f, size = shape ? .4f + .3f * mu.level : 1.f;
            const float ax = mu.clock * .0009f * dir + i, ay = mu.clock * .0013f * dir, az = mu.clock * .0005f;
            P2 p[8];
            for (int v = 0; v < 8; ++v)
                p[v] = project(rotate({(v & 1 ? 1.f : -1.f) * size, (v & 2 ? 1.f : -1.f) * size,
                                       (v & 4 ? 1.f : -1.f) * size}, ax, ay, az), cx, cy, scale);
            for (const auto& edge : kEdges)
                line(c, p[edge[0]], p[edge[1]],
                     depth_ink(t, (p[edge[0]].depth + p[edge[1]].depth) * .5f - mu.kick * .6f, shape ? t.vizWave : t.textTitle));
        }
    }
}

void globe(Canvas& c, int x, int y, int w, int h, const Theme& t, const Music& mu) {
    const int n = copies_for(w, h);
    const float scale = float(std::min(h, w / n)) * .33f * (1.f + .12f * mu.kick);
    for (int i = 0; i < n; ++i) {
        const int cx = x + (2 * i + 1) * w / (2 * n), cy = y + h / 2;
        const float yaw = mu.clock * .0008f + i, tilt = .35f;
        // A hit bulges the equator out and lets it settle back.
        const auto at = [&](float lat, float lon) {
            const float cl = std::cos(lat), bulge = 1.f + .22f * mu.kick * cl * cl * cl * cl;
            return project(rotate({cl * std::cos(lon) * bulge, std::sin(lat), cl * std::sin(lon) * bulge},
                                  tilt, yaw, 0.f), cx, cy, scale);
        };
        for (int la = -2; la <= 2; ++la) {   // latitudes
            const float lat = float(la) * kPi / 7.f;
            P2 prev = at(lat, 0.f);
            for (int s = 1; s <= 20; ++s) {
                const P2 q = at(lat, float(s) * 2.f * kPi / 20.f);
                line(c, prev, q, depth_ink(t, (prev.depth + q.depth) * .5f, t.vizWave));
                prev = q;
            }
        }
        for (int lo = 0; lo < 8; ++lo) {     // longitudes
            const float lon = float(lo) * 2.f * kPi / 8.f;
            P2 prev = at(-kPi * .5f, lon);
            for (int s = 1; s <= 12; ++s) {
                const P2 q = at(-kPi * .5f + float(s) * kPi / 12.f, lon);
                line(c, prev, q, depth_ink(t, (prev.depth + q.depth) * .5f, t.rowCursor));
                prev = q;
            }
        }
        // A tilted orbit with a satellite, like the splash's; the satellite runs with the song.
        P2 prev = project(rotate({1.45f, 0.f, 0.f}, .5f, yaw * .3f, .2f), cx, cy, scale);
        for (int s = 1; s <= 36; ++s) {
            const float a = float(s) * 2.f * kPi / 36.f;
            const P2 q = project(rotate({std::cos(a) * 1.45f, 0.f, std::sin(a) * 1.45f}, .5f, yaw * .3f, .2f), cx, cy, scale);
            line(c, prev, q, depth_ink(t, q.depth, t.textTitle));
            prev = q;
        }
        const float sa = mu.clock * .0025f + i;
        const P2 sat = project(rotate({std::cos(sa) * 1.45f, 0.f, std::sin(sa) * 1.45f}, .5f, yaw * .3f, .2f), cx, cy, scale);
        const int dot = 3 + int(mu.kick * 3.f);
        c.fill_rect(sat.x - dot / 2, sat.y - dot / 2, dot, dot, t.textValue);
    }
}

void terrain(Canvas& c, int x, int y, int w, int h, const Theme& t, const float* wave, const Music& mu) {
    constexpr int kRows = 18, kCols = 28;
    const int cx = x + w / 2, horizon = y + h * 3 / 10;
    const float focal = float(h) * .9f, travel = mu.clock * .0018f, scroll = std::fmod(travel, 1.f);
    // A striped sun sitting on the horizon; a hit lifts it.
    const int sun = std::max(4, int(float(h) / 6.f * (1.f + .15f * mu.kick)));
    for (int band = 0; band < sun; band += 3)
        c.fill_rect(cx - int(std::sqrt(float(sun * sun - band * band))), horizon - band - 2,
                    2 * int(std::sqrt(float(sun * sun - band * band))), 2, mix_rgb(t.textTitle, t.rowCursor, band * 100 / sun));
    const auto height = [&](float xw, float zw) {
        const float hills = (std::sin(xw * .9f + zw * .7f) + std::sin(xw * .37f - zw * 1.3f + 1.f)) * .22f;
        const float near = wave_at(wave, std::clamp((xw + 4.f) / 8.f, 0.f, 1.f)) * .4f * mu.level;
        return std::max(0.f, std::fabs(xw) > .7f ? hills * (1.f + 1.2f * mu.level + .8f * mu.kick) + near : near * .4f);
    };
    P2 grid[kRows][kCols];
    for (int r = 0; r < kRows; ++r) {
        const float z = 1.f + float(r) - scroll;   // 1 near … far
        for (int col = 0; col < kCols; ++col) {
            const float xw = -4.f + 8.f * float(col) / float(kCols - 1);
            const float lift = height(xw, z + travel);
            grid[r][col] = {cx + int(xw * focal / z), horizon + int((1.f - lift) * focal / z), z / float(kRows)};
        }
    }
    for (int r = kRows - 1; r >= 0; --r) {
        const Argb ink = mix_rgb(ramp(t, 90), t.vizWave, std::clamp(100 - int(grid[r][0].depth * 110.f), 0, 100));
        for (int col = 1; col < kCols; ++col) line(c, grid[r][col - 1], grid[r][col], ink);
        if (r > 0)
            for (int col = 0; col < kCols; col += 2) line(c, grid[r][col], grid[r - 1][col], ink);
    }
}

// A turning ring doughnut; the tube swells on a hit and a ripple runs round it with the song.
void torus(Canvas& c, int x, int y, int w, int h, const Theme& t, const Music& mu) {
    constexpr int kMajor = 18, kMinor = 10;
    const int n = copies_for(w, h);
    const float scale = float(std::min(h, w / n)) * .28f * (1.f + .1f * mu.kick);
    for (int i = 0; i < n; ++i) {
        const int cx = x + (2 * i + 1) * w / (2 * n), cy = y + h / 2;
        const float ax = mu.clock * .0007f + i, ay = mu.clock * .0011f, az = mu.clock * .0004f;
        P2 p[kMajor][kMinor];
        for (int a = 0; a < kMajor; ++a)
            for (int b = 0; b < kMinor; ++b) {
                const float u = float(a) * 2.f * kPi / kMajor, v = float(b) * 2.f * kPi / kMinor;
                const float tube = .38f * (1.f + .35f * mu.kick + .25f * mu.level * std::sin(3.f * u - mu.clock * .004f));
                const float ring = 1.f + tube * std::cos(v);
                p[a][b] = project(rotate({ring * std::cos(u), tube * std::sin(v), ring * std::sin(u)}, ax, ay, az),
                                  cx, cy, scale * .72f);
            }
        for (int a = 0; a < kMajor; ++a)
            for (int b = 0; b < kMinor; ++b) {
                const P2& q = p[a][b];
                const P2& nextMinor = p[a][(b + 1) % kMinor];
                const P2& nextMajor = p[(a + 1) % kMajor][b];
                line(c, q, nextMinor, depth_ink(t, (q.depth + nextMinor.depth) * .5f, t.vizWave));
                line(c, q, nextMajor, depth_ink(t, (q.depth + nextMajor.depth) * .5f, t.rowCursor));
            }
    }
}

// A ball of dots; a ripple runs over it with the song and a hit pushes the dots out.
void dotball(Canvas& c, int x, int y, int w, int h, const Theme& t, const Music& mu) {
    constexpr int kDots = 96;
    const int n = copies_for(w, h);
    const float scale = float(std::min(h, w / n)) * .33f;
    for (int i = 0; i < n; ++i) {
        const int cx = x + (2 * i + 1) * w / (2 * n), cy = y + h / 2;
        const float ax = mu.clock * .0006f + i, ay = mu.clock * .0010f;
        P2 p[kDots];
        int order[kDots];
        for (int d = 0; d < kDots; ++d) {
            // A Fibonacci sphere: the dots spread evenly over the surface.
            const float k = (float(d) + .5f) / kDots, lat = std::acos(1.f - 2.f * k), lon = float(d) * 2.39996f;
            const float r = 1.f + .18f * mu.kick + .1f * mu.level * std::sin(lat * 5.f - mu.clock * .005f);
            p[d] = project(rotate({std::sin(lat) * std::cos(lon) * r, std::cos(lat) * r, std::sin(lat) * std::sin(lon) * r},
                                  ax, ay, 0.f), cx, cy, scale);
            order[d] = d;
        }
        // Far ones first, so near dots cover them.
        std::sort(order, order + kDots, [&](int a, int b) { return p[a].depth > p[b].depth; });
        for (int k = 0; k < kDots; ++k) {
            const P2& q = p[order[k]];
            const int size = std::max(1, int((1.4f - q.depth) * scale * (.04f + .02f * mu.kick)));
            c.fill_rect(q.x - size / 2, q.y - size / 2, size, size,
                        mix_rgb(ramp(t, 120), order[k] % 3 ? t.vizWave : t.textTitle,
                                std::clamp(int((1.f - q.depth) * 50.f), 0, 100)));
        }
    }
}

void ridges(Canvas& c, int x, int y, int w, int h, const Theme& t, uint64_t ms, const float* wave, EffectMemory& m) {
    constexpr int kRows = EffectMemory::kRidgeRows, kPoints = EffectMemory::kRidgePoints;
    // A new line every 60 ms of a running clock; none while it holds still.
    if (ms != m.ridgeMs && ms - m.ridgeMs >= 60) {
        m.ridgeMs = ms;
        m.ridgeHead = (m.ridgeHead + 1) % kRows;
        for (int i = 0; i < kPoints; ++i) {
            float sum = 0.f;
            for (int k = 0; k < 4; ++k) sum += wave_at(wave, (float(i) * 4.f + float(k)) / float(kPoints * 4));
            m.ridges[m.ridgeHead][i] = int8_t(std::clamp(int(sum * 40.f), -127, 127));
        }
    }
    const int cx = x + w / 2;
    for (int k = kRows - 1; k >= 0; --k) {   // the oldest at the back, drawn first
        const auto& row = m.ridges[(m.ridgeHead - k + kRows) % kRows];
        const float shrink = 1.f - float(k) * .022f;
        const int base = y + h - 4 - k * (h - 12) / kRows;
        const float amp = float(h) * .22f;
        const Argb ink = mix_rgb(ramp(t, 70), t.vizWave, 100 - k * 100 / kRows);
        int px = 0, py = 0;
        for (int i = 0; i < kPoints; ++i) {
            const float along = float(i) / float(kPoints - 1);
            const float env = std::sin(along * kPi) * std::sin(along * kPi);
            const int nx = cx + int((along - .5f) * float(w) * .86f * shrink);
            const int ny = base - int((std::fabs(float(row[i])) / 127.f * amp + 2.f) * env);
            if (i) {
                // Cover what is behind, then draw the line: the near ridges hide the far ones.
                c.fill_rect(std::min(px, nx), std::min(py, ny), std::abs(nx - px) + 1, base - std::min(py, ny) + 2,
                            t.vizBackground);
                c.line(px, py, nx, ny, ink);
            }
            px = nx; py = ny;
        }
    }
}

}  // namespace

bool is_style(VisualizerType v) { return static_cast<int>(v) >= static_cast<int>(VisualizerType::TUNNEL); }

void style(Canvas& c, int x, int y, int w, int h, const Theme& t, VisualizerType v, uint64_t ms,
           const float* wave, float energy, EffectMemory& memory) {
    const Music mu = listen(memory, ms, std::clamp(energy, 0.f, 1.f));
    switch (v) {
        case VisualizerType::TUNNEL:    tunnel(c, x, y, w, h, t, mu); break;
        case VisualizerType::FIRE:      fire(c, x, y, w, h, t, ms, mu, memory); break;
        case VisualizerType::RAIN:      rain(c, x, y, w, h, t, ms, mu); break;
        case VisualizerType::TWISTER:   twister(c, x, y, w, h, t, mu); break;
        case VisualizerType::RINGS:     rings(c, x, y, w, h, t, wave, mu); break;
        case VisualizerType::LISSAJOUS: lissajous(c, x, y, w, h, t, wave, mu); break;
        case VisualizerType::BLOBS:     blobs(c, x, y, w, h, t, mu); break;
        case VisualizerType::ROTOZOOM:  rotozoom(c, x, y, w, h, t, mu); break;
        case VisualizerType::CUBE:      cube(c, x, y, w, h, t, mu); break;
        case VisualizerType::GLOBE:     globe(c, x, y, w, h, t, mu); break;
        case VisualizerType::TERRAIN:   terrain(c, x, y, w, h, t, wave, mu); break;
        case VisualizerType::TORUS:     torus(c, x, y, w, h, t, mu); break;
        case VisualizerType::DOTBALL:   dotball(c, x, y, w, h, t, mu); break;
        case VisualizerType::RIDGES:    ridges(c, x, y, w, h, t, ms, wave, memory); break;
        default: break;
    }
}

}  // namespace pt::ui::demo_fx
