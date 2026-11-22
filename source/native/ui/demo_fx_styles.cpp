// The fourteen visualizer styles after PLASMA (specs/more-visualizers.md): classic demoscene effects
// written for TrackWerk's software canvas. Eight are flat (TUNNEL … MAZE), six are light 3D
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

// ── 2D ──────────────────────────────────────────────────────────────────────────────────────────

// Flying down an endless chequered tube: the speed follows the song and rushes on a hit, the tube
// sways on a slow curve, and a hit pulls the walls in and lights them.
void tunnel(Canvas& c, int x, int y, int w, int h, const Theme& t, const Music& mu) {
    const int cell = cell_for(w, h);
    const float reach = std::max(1.f, std::min(w, h) * .65f);
    const float spin = mu.clock * .00008f, travel = mu.clock * .022f;
    const float swayX = std::sin(mu.clock * .00045f) * w * .14f, swayY = std::cos(mu.clock * .00031f) * h * .14f;
    const float squeeze = 1.f + .4f * mu.kick;
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
            const int shade = (pattern ? 160 + int(mu.drive * 60.f) : 70) * depth / 255 + int(mu.kick * 90.f * depth / 255);
            c.fill_rect(x + px, y + py, std::min(cell, w - px), std::min(cell, h - py), ramp(t, std::min(255, shade)));
        }
}

// Classic fire, stepped at a calm 30 steps a second whatever the frame rate; the song lifts the
// flames and a hit flares them.
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
            const int heat = 150 + int(mu.drive * 70.f + mu.kick * 35.f);
            for (int col = 0; col < cols; ++col)
                at(rows - 1, col) = (mix32(tick * 7919U + uint32_t(col)) & 255) > 90 ? uint8_t(std::min(255, heat)) : 0;
            // Each cell cools a little as it rises; more drive, taller flames.
            const int cool = std::max(1, int(190.f * (1.35f - .8f * mu.drive)) / rows);
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

// Falling code in small glyphs, dense columns at their own speeds; the song speeds the fall and
// lengthens the trails, and a hit lights every column's head.
void rain(Canvas& c, int x, int y, int w, int h, const Theme& t, uint64_t ms, const Music& mu) {
    static constexpr char kGlyphs[] = "0123456789ABCDEF<>+-=*#:";
    constexpr int stepX = 7, stepY = 8;
    const int rows = h / stepY + 1;
    const Argb head = mix_rgb(t.textValue, t.rowCursor, int(mu.kick * 70.f));
    for (int col = 0; col * stepX < w; ++col) {
        const uint32_t r = mix32(uint32_t(col) * 2654435761U + 5U);
        const int trail = 5 + int(r % 10) + int(mu.drive * 8.f);
        const float speed = 3.f + float((r >> 4) % 6);   // rows a second at the quiet pace
        const int pos = int(mu.clock * speed / 1000.f + float((r >> 9) % 97)) % (rows + 24);
        for (int k = 0; k < trail; ++k) {
            const int row = pos - k;
            if (row < 0 || row >= rows) continue;
            const uint32_t pick = mix32(uint32_t(col * 131 + row * 17) + (k == 0 ? uint32_t(ms / 150) : 0U));
            const char ch = kGlyphs[pick % (sizeof(kGlyphs) - 1)];
            const Argb ink = k == 0 ? head : mix_rgb(ramp(t, 50), t.vizWave, 100 - k * 100 / trail);
            c.draw_glyph(glyph_for_codepoint(uint32_t(ch)), x + col * stepX + 1, y + row * stepY, ink, 1);
        }
    }
}

void twister(Canvas& c, int x, int y, int w, int h, const Theme& t, const Music& mu) {
    const int count = std::clamp(w / 280, 1, 3);
    const float radius = std::max(6.f, std::min(float(h) * .42f, float(w) / float(count * 4))) * (1.f + .12f * mu.kick);
    const Argb faces[4] = {ramp(t, 170), t.vizWave, ramp(t, 230), t.rowSelection};
    for (int i = 0; i < count; ++i) {
        const int cx = x + (2 * i + 1) * w / (2 * count);
        for (int py = 0; py < h; py += 2) {
            const float a = std::sin(float(py) * .018f + mu.clock * .0012f + float(i) * 1.7f) * (.7f + 1.f * mu.drive) +
                            mu.clock * .0007f;
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

// Shockwaves: every hit fires a bright ring that races out and fades; faint rings drift out between
// them, and the waveform bends the core.
void rings(Canvas& c, int x, int y, int w, int h, const Theme& t, const float* wave, const Music& mu, EffectMemory& m) {
    const int cx = x + w / 2, cy = y + h / 2;
    const float squash = std::min(1.f, float(h) / float(w) * 1.8f);
    const float reach = std::sqrt(float(w * w) * .25f + float(h * h) * .25f / (squash * squash));
    const auto ring = [&](float r, Argb ink, int thick) {
        for (int k = 0; k < thick; ++k) {
            const float rr = r + float(k);
            int px = cx + int(rr), py = cy;
            for (int s = 1; s <= 48; ++s) {
                const float a = float(s) * 2.f * kPi / 48.f;
                const int nx = cx + int(std::cos(a) * rr), ny = cy + int(std::sin(a) * rr * squash);
                c.line(px, py, nx, ny, ink);
                px = nx; py = ny;
            }
        }
    };
    for (int k = 0; k < 6; ++k) {
        const float r = std::fmod(mu.clock * .03f + float(k) * reach / 6.f, reach);
        ring(r, ramp(t, 140 - int(r / reach * 90.f)), 1);
    }
    // Fire one on each hit (the kick rising past a threshold after it fell back).
    if (mu.kick > .45f && m.kickArmed) {
        m.ringBorn[m.ringNext] = mu.clock;
        m.ringNext = (m.ringNext + 1) % 8;
        m.kickArmed = false;
    } else if (mu.kick < .2f) {
        m.kickArmed = true;
    }
    for (const float born : m.ringBorn) {
        if (born < 0.f || born > mu.clock) continue;
        const float r = (mu.clock - born) * reach / 700.f;
        if (r >= reach) continue;
        const int fade = 100 - int(r / reach * 100.f);
        ring(r, mix_rgb(t.vizBackground, mix_rgb(t.textValue, t.rowCursor, 100 - fade), fade), 1 + fade / 40);
    }
    // The waveform bends the core; a hit swells it.
    const float base = std::min(float(w), float(h) / squash) * (.16f + .1f * mu.kick);
    int px = 0, py = 0;
    for (int s = 0; s <= 96; ++s) {
        const float a = float(s % 96) * 2.f * kPi / 96.f;
        const float r = base * (1.f + std::clamp(.5f * mu.gain * wave_at(wave, float(s % 96) / 96.f), -.8f, .8f));
        const int nx = cx + int(std::cos(a) * r), ny = cy + int(std::sin(a) * r * squash);
        if (s) c.line(px, py, nx, ny, t.vizWave);
        px = nx; py = ny;
    }
}

// A kaleidoscope: a flowing pattern folded into six mirrored slices, turning with the song; a hit
// zooms it in and lights it.
void kaleido(Canvas& c, int x, int y, int w, int h, const Theme& t, const Music& mu) {
    const int cell = cell_for(w, h);
    const float rot = mu.clock * .00025f, tt = mu.clock * .0015f;
    const float zoom = 4.5f / float(std::min(w, h)) * (1.f - .4f * mu.kick);
    constexpr float seg = kPi / 3.f;   // a slice and its mirror: six of each round the circle
    for (int py = 0; py < h; py += cell)
        for (int px = 0; px < w; px += cell) {
            const float dx = px + cell * .5f - w * .5f, dy = (py + cell * .5f - h * .5f) * 1.2f;
            const float r = std::sqrt(dx * dx + dy * dy) * zoom;
            float a = std::atan2(dy, dx) + rot;
            a -= std::floor(a / (2.f * seg)) * 2.f * seg;
            if (a > seg) a = 2.f * seg - a;
            const float u = r * std::cos(a), v = r * std::sin(a);
            const float f = std::sin(u * 1.7f + tt) + std::sin(v * 2.3f - tt * 1.3f) + std::sin((u + v) * 1.1f + tt * .7f);
            const int band = int((f + 3.f) * 42.f);   // 0 … 252
            const int shade = (band & 32 ? 150 + int(mu.drive * 70.f) : 60) + (band & 31) * 2 + int(mu.kick * 60.f);
            c.fill_rect(x + px, y + py, std::min(cell, w - px), std::min(cell, h - py), ramp(t, std::min(255, shade)));
        }
}

// Metaballs: soft drops of liquid drifting slowly, merging where they touch, a thin bright skin at
// the edge; the song swells them a little.
void blobs(Canvas& c, int x, int y, int w, int h, const Theme& t, const Music& mu) {
    const int cell = cell_for(w, h);
    constexpr int kBlobs = 6;
    float bx[kBlobs], by[kBlobs], rr[kBlobs];
    const float size = float(std::min(w, h)) * (.075f + .02f * mu.drive + .015f * mu.kick);
    for (int k = 0; k < kBlobs; ++k) {
        bx[k] = w * (.5f + .4f * std::sin(mu.clock * (.00018f + k * .00005f) + k * 1.3f));
        by[k] = h * (.5f + .38f * std::cos(mu.clock * (.00022f + k * .00004f) + k * 2.1f));
        rr[k] = size * size * (.7f + .12f * k);
    }
    for (int py = 0; py < h; py += cell)
        for (int px = 0; px < w; px += cell) {
            float f = 0.f;
            for (int k = 0; k < kBlobs; ++k) {
                const float dx = px - bx[k], dy = py - by[k];
                f += rr[k] / (dx * dx + dy * dy + 1.f);
            }
            Argb ink;
            if (f < .6f) ink = ramp(t, int(f * 70.f));                        // the glow around a drop
            else if (f < .74f) ink = mix_rgb(ramp(t, 140), t.vizWave, 55 + int(mu.kick * 30.f));   // its thin skin
            else ink = ramp(t, std::min(150, 85 + int((f - .74f) * 30.f)));    // inside, darker toward the edge
            c.fill_rect(x + px, y + py, std::min(cell, w - px), std::min(cell, h - py), ink);
        }
}

// A maze (the classic one-line diagonal maze) seen through a slowly turning, breathing lens; a hit
// leans the lens in and lights the walls.
void maze(Canvas& c, int x, int y, int w, int h, const Theme& t, const Music& mu) {
    const int cell = cell_for(w, h);
    constexpr int kTile = 40, kWall = 5;
    const float a = mu.clock * .00012f;
    const float zoom = (1.1f + .45f * std::sin(mu.clock * .00017f)) * (1.f - .18f * mu.kick);
    const float ca = std::cos(a) * zoom, sa = std::sin(a) * zoom;
    const float driftX = mu.clock * .012f, driftY = mu.clock * .007f;
    const Argb wall = mix_rgb(t.vizWave, t.textValue, int(mu.kick * 50.f)), floorInk = ramp(t, 40);
    for (int py = 0; py < h; py += cell)
        for (int px = 0; px < w; px += cell) {
            const float dx = px + cell * .5f - w * .5f, dy = py + cell * .5f - h * .5f;
            const int u = int(dx * ca - dy * sa + driftX + 65536.f), v = int(dx * sa + dy * ca + driftY + 65536.f);
            const int fu = u % kTile, fv = v % kTile;
            // Each tile holds one diagonal, this way or that; together they make the maze.
            const bool slash = mix32(uint32_t(u / kTile) * 73856093U ^ uint32_t(v / kTile) * 19349663U) & 1;
            const int off = slash ? std::abs(fu - fv) : std::abs(fu + fv - kTile);
            Argb ink = floorInk;
            if (off < kWall) ink = wall;
            c.fill_rect(x + px, y + py, std::min(cell, w - px), std::min(cell, h - py), ink);
        }
}

// ── light 3D ────────────────────────────────────────────────────────────────────────────────────
// They turn on the music clock (faster with the song, a rush on a hit, never jerking) and punch out
// on a hit.

void cube(Canvas& c, int x, int y, int w, int h, const Theme& t, const Music& mu) {
    static constexpr int kEdges[12][2] = {{0, 1}, {1, 3}, {3, 2}, {2, 0}, {4, 5}, {5, 7},
                                          {7, 6}, {6, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    const int n = copies_for(w, h);
    const float scale = float(std::min(h, w / n)) * .17f * (1.f + .45f * mu.kick);
    for (int i = 0; i < n; ++i) {
        const int cx = x + (2 * i + 1) * w / (2 * n), cy = y + h / 2;
        for (int shape = 0; shape < 2; ++shape) {   // the outer cube, then an inner one turning the other way
            const float dir = shape ? -1.3f : 1.f, size = shape ? .3f + .5f * mu.drive : 1.f;
            const float ax = mu.clock * .0009f * dir + i, ay = mu.clock * .0013f * dir, az = mu.clock * .0005f;
            P2 p[8];
            for (int v = 0; v < 8; ++v)
                p[v] = project(rotate({(v & 1 ? 1.f : -1.f) * size, (v & 2 ? 1.f : -1.f) * size,
                                       (v & 4 ? 1.f : -1.f) * size}, ax, ay, az), cx, cy, scale);
            for (const auto& edge : kEdges)
                line(c, p[edge[0]], p[edge[1]],
                     depth_ink(t, (p[edge[0]].depth + p[edge[1]].depth) * .5f - mu.kick * 1.2f, shape ? t.vizWave : t.textTitle));
        }
    }
}

// A wireframe globe with spikes all over it that shoot out with the song; a hit throws them long.
void globe(Canvas& c, int x, int y, int w, int h, const Theme& t, const Music& mu) {
    const int n = copies_for(w, h);
    const float scale = float(std::min(h, w / n)) * .27f;
    for (int i = 0; i < n; ++i) {
        const int cx = x + (2 * i + 1) * w / (2 * n), cy = y + h / 2;
        const float yaw = mu.clock * .0008f + i, tilt = .35f;
        const auto point = [&](float lat, float lon, float r) {
            return project(rotate({std::cos(lat) * std::cos(lon) * r, std::sin(lat) * r, std::cos(lat) * std::sin(lon) * r},
                                  tilt, yaw, 0.f), cx, cy, scale);
        };
        for (int la = -2; la <= 2; ++la) {   // latitudes
            const float lat = float(la) * kPi / 7.f;
            P2 prev = point(lat, 0.f, 1.f);
            for (int s = 1; s <= 20; ++s) {
                const P2 q = point(lat, float(s) * 2.f * kPi / 20.f, 1.f);
                line(c, prev, q, depth_ink(t, (prev.depth + q.depth) * .5f, t.vizWave));
                prev = q;
            }
        }
        for (int lo = 0; lo < 8; ++lo) {     // longitudes
            const float lon = float(lo) * 2.f * kPi / 8.f;
            P2 prev = point(-kPi * .5f, lon, 1.f);
            for (int s = 1; s <= 12; ++s) {
                const P2 q = point(-kPi * .5f + float(s) * kPi / 12.f, lon, 1.f);
                line(c, prev, q, depth_ink(t, (prev.depth + q.depth) * .5f, t.rowCursor));
                prev = q;
            }
        }
        // The spikes: each its own length, rising and falling with the song.
        for (int la = -2; la <= 2; ++la)
            for (int s = 0; s < 20; s += 2) {
                const float lat = float(la) * kPi / 7.f, lon = float(s + (la & 1)) * 2.f * kPi / 20.f;
                const float own = .5f + .5f * std::sin(float(s * 7 + la * 13) + mu.clock * .004f);
                const float len = .06f + own * (.35f * mu.drive + .55f * mu.kick);
                const P2 a = point(lat, lon, 1.f), b = point(lat, lon, 1.f + len);
                line(c, a, b, depth_ink(t, a.depth - mu.kick, t.textValue));
            }
    }
}

void terrain(Canvas& c, int x, int y, int w, int h, const Theme& t, const float* wave, const Music& mu) {
    constexpr int kRows = 18, kCols = 28;
    const int cx = x + w / 2, horizon = y + h * 3 / 10;
    const float focal = float(h) * .9f, travel = mu.clock * .0018f, scroll = std::fmod(travel, 1.f);
    // A striped sun sitting on the horizon; a hit swells it.
    const int sun = std::max(4, int(float(h) / 6.f * (1.f + .3f * mu.kick)));
    for (int band = 0; band < sun; band += 3)
        c.fill_rect(cx - int(std::sqrt(float(sun * sun - band * band))), horizon - band - 2,
                    2 * int(std::sqrt(float(sun * sun - band * band))), 2, mix_rgb(t.textTitle, t.rowCursor, band * 100 / sun));
    const auto height = [&](float xw, float zw) {
        const float hills = (std::sin(xw * .9f + zw * .7f) + std::sin(xw * .37f - zw * 1.3f + 1.f)) * .22f;
        const float near = std::clamp(wave_at(wave, std::clamp((xw + 4.f) / 8.f, 0.f, 1.f)) * mu.gain, -1.f, 1.f) * .35f * mu.drive;
        return std::max(0.f, std::fabs(xw) > .7f ? hills * (.6f + 1.8f * mu.drive + 1.2f * mu.kick) + near : near * .4f);
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
        const Argb ink = mix_rgb(ramp(t, 90), t.vizWave, std::clamp(100 - int(grid[r][0].depth * 110.f) + int(mu.kick * 30.f), 0, 100));
        for (int col = 1; col < kCols; ++col) line(c, grid[r][col - 1], grid[r][col], ink);
        if (r > 0)
            for (int col = 0; col < kCols; col += 2) line(c, grid[r][col], grid[r - 1][col], ink);
    }
}

// A turning ring doughnut; a ripple runs round the tube with the song and a hit swells it.
void torus(Canvas& c, int x, int y, int w, int h, const Theme& t, const Music& mu) {
    constexpr int kMajor = 18, kMinor = 10;
    const int n = copies_for(w, h);
    const float scale = float(std::min(h, w / n)) * .26f * (1.f + .15f * mu.kick);
    for (int i = 0; i < n; ++i) {
        const int cx = x + (2 * i + 1) * w / (2 * n), cy = y + h / 2;
        const float ax = mu.clock * .0007f + i, ay = mu.clock * .0011f, az = mu.clock * .0004f;
        P2 p[kMajor][kMinor];
        for (int a = 0; a < kMajor; ++a)
            for (int b = 0; b < kMinor; ++b) {
                const float u = float(a) * 2.f * kPi / kMajor, v = float(b) * 2.f * kPi / kMinor;
                const float tube = .34f * (1.f + .6f * mu.kick + .45f * mu.drive * std::sin(3.f * u - mu.clock * .004f));
                const float ring = 1.f + tube * std::cos(v);
                p[a][b] = project(rotate({ring * std::cos(u), tube * std::sin(v), ring * std::sin(u)}, ax, ay, az),
                                  cx, cy, scale * .72f);
            }
        for (int a = 0; a < kMajor; ++a)
            for (int b = 0; b < kMinor; ++b) {
                const P2& q = p[a][b];
                const P2& nextMinor = p[a][(b + 1) % kMinor];
                const P2& nextMajor = p[(a + 1) % kMajor][b];
                line(c, q, nextMinor, depth_ink(t, (q.depth + nextMinor.depth) * .5f - mu.kick, t.vizWave));
                line(c, q, nextMajor, depth_ink(t, (q.depth + nextMajor.depth) * .5f, t.rowCursor));
            }
    }
}

// A ball of dots; a ripple runs over it with the song and a hit blows the dots out.
void dotball(Canvas& c, int x, int y, int w, int h, const Theme& t, const Music& mu) {
    constexpr int kDots = 96;
    const int n = copies_for(w, h);
    const float scale = float(std::min(h, w / n)) * .3f;
    for (int i = 0; i < n; ++i) {
        const int cx = x + (2 * i + 1) * w / (2 * n), cy = y + h / 2;
        const float ax = mu.clock * .0006f + i, ay = mu.clock * .0010f;
        P2 p[kDots];
        int order[kDots];
        for (int d = 0; d < kDots; ++d) {
            // A Fibonacci sphere: the dots spread evenly over the surface.
            const float k = (float(d) + .5f) / kDots, lat = std::acos(1.f - 2.f * k), lon = float(d) * 2.39996f;
            const float r = 1.f + .4f * mu.kick + .2f * mu.drive * std::sin(lat * 5.f - mu.clock * .005f);
            p[d] = project(rotate({std::sin(lat) * std::cos(lon) * r, std::cos(lat) * r, std::sin(lat) * std::sin(lon) * r},
                                  ax, ay, 0.f), cx, cy, scale);
            order[d] = d;
        }
        // Far ones first, so near dots cover them.
        std::sort(order, order + kDots, [&](int a, int b) { return p[a].depth > p[b].depth; });
        for (int k = 0; k < kDots; ++k) {
            const P2& q = p[order[k]];
            const int size = std::max(1, int((1.4f - q.depth) * scale * (.035f + .03f * mu.kick)));
            c.fill_rect(q.x - size / 2, q.y - size / 2, size, size,
                        mix_rgb(ramp(t, 120), order[k] % 3 ? t.vizWave : t.textTitle,
                                std::clamp(int((1.f - q.depth) * 50.f + mu.kick * 50.f), 0, 100)));
        }
    }
}

// Joy Division ridges from the waveform, one line every 60 ms; a quiet song is lifted to size.
void ridges(Canvas& c, int x, int y, int w, int h, const Theme& t, uint64_t ms, const float* wave, const Music& mu,
            EffectMemory& m) {
    constexpr int kRows = EffectMemory::kRidgeRows, kPoints = EffectMemory::kRidgePoints;
    // A new line every 60 ms of a running clock; none while it holds still.
    if (ms != m.ridgeMs && ms - m.ridgeMs >= 60) {
        m.ridgeMs = ms;
        m.ridgeHead = (m.ridgeHead + 1) % kRows;
        for (int i = 0; i < kPoints; ++i) {
            float sum = 0.f;
            for (int k = 0; k < 4; ++k) sum += wave_at(wave, (float(i) * 4.f + float(k)) / float(kPoints * 4));
            m.ridges[m.ridgeHead][i] = int8_t(std::clamp(int(sum * 30.f * std::min(mu.gain, 1.8f)), -127, 127));
        }
    }
    const int cx = x + w / 2;
    for (int k = kRows - 1; k >= 0; --k) {   // the oldest at the back, drawn first
        const auto& row = m.ridges[(m.ridgeHead - k + kRows) % kRows];
        const float shrink = 1.f - float(k) * .022f;
        const int base = y + h - 4 - k * (h - 12) / kRows;
        const float amp = float(h) * .17f;
        const Argb ink = mix_rgb(ramp(t, 70), k == 0 ? mix_rgb(t.vizWave, t.textValue, int(mu.kick * 35.f)) : t.vizWave,
                                 100 - k * 100 / kRows);
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

// The loudness of one frame is noisy, and time × loudness jumps back and forth as it changes, which
// reads as shaking; so every visualizer listens through this instead (demo_fx.h Music).
Music listen(EffectMemory& m, uint64_t ms, float e) {
    const auto drive_of = [&m] {
        return .8f * std::min(1.f, m.level / std::max(.08f, m.peak)) + .2f * m.level;
    };
    if (ms != m.pulseMs) {
        const float dt = m.pulseMs == 0 || ms < m.pulseMs ? 16.f : std::min(100.f, float(ms - m.pulseMs));
        m.pulseMs = ms;
        m.level += (e - m.level) * std::min(1.f, dt / (e > m.level ? 40.f : 300.f));
        // This song's own peak, forgotten over several seconds: what "loud" means right now.
        m.peak = std::max(m.level, m.peak * std::exp(-dt / 5000.f));
        const float norm = std::min(1.f, m.level / std::max(.08f, m.peak));
        m.slowNorm += (norm - m.slowNorm) * std::min(1.f, dt / 500.f);
        m.kick = std::max(m.kick * std::exp(-dt / 220.f), std::clamp((norm - m.slowNorm) * 3.5f, 0.f, 1.f));
        // Forward only: 0.5x in silence, faster as the song drives, a rush on a hit (at most 2.9x).
        m.musicClock += dt * (.5f + 1.3f * drive_of() + 1.1f * m.kick);
    }
    return {m.musicClock, drive_of(), m.kick, std::clamp(.6f / std::max(.1f, m.peak), 1.f, 4.f)};
}

bool is_style(VisualizerType v) { return static_cast<int>(v) >= static_cast<int>(VisualizerType::TUNNEL); }

void style(Canvas& c, int x, int y, int w, int h, const Theme& t, VisualizerType v, uint64_t ms,
           const float* wave, float energy, EffectMemory& memory) {
    const Music mu = listen(memory, ms, std::clamp(energy, 0.f, 1.f));
    switch (v) {
        case VisualizerType::TUNNEL:    tunnel(c, x, y, w, h, t, mu); break;
        case VisualizerType::FIRE:      fire(c, x, y, w, h, t, ms, mu, memory); break;
        case VisualizerType::RAIN:      rain(c, x, y, w, h, t, ms, mu); break;
        case VisualizerType::TWISTER:   twister(c, x, y, w, h, t, mu); break;
        case VisualizerType::RINGS:     rings(c, x, y, w, h, t, wave, mu, memory); break;
        case VisualizerType::KALEIDO:   kaleido(c, x, y, w, h, t, mu); break;
        case VisualizerType::BLOBS:     blobs(c, x, y, w, h, t, mu); break;
        case VisualizerType::MAZE:      maze(c, x, y, w, h, t, mu); break;
        case VisualizerType::CUBE:      cube(c, x, y, w, h, t, mu); break;
        case VisualizerType::GLOBE:     globe(c, x, y, w, h, t, mu); break;
        case VisualizerType::TERRAIN:   terrain(c, x, y, w, h, t, wave, mu); break;
        case VisualizerType::TORUS:     torus(c, x, y, w, h, t, mu); break;
        case VisualizerType::DOTBALL:   dotball(c, x, y, w, h, t, mu); break;
        case VisualizerType::RIDGES:    ridges(c, x, y, w, h, t, ms, wave, mu, memory); break;
        default: break;
    }
}

}  // namespace pt::ui::demo_fx
