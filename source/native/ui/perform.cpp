#include "ui/perform.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "audio-engine.h"
#include "songcore/host.h"
#include "studio/synth.h"
#include "ui/app_state.h"
#include "ui/canvas.h"
#include "ui/helpers.h"
#include "ui/premium.h"
#include "ui/theme.h"

namespace pt::ui {

namespace {
using songcore::PerformSlot;
using Type = PerformSlot::Type;

// The synth code an instrument plays, or −1 when it is not a Studio synth.
int synth_code(const songcore::Project& p, int instrument) {
    if (instrument < 0 || instrument >= int(p.instruments.size())) return -1;
    const auto& ins = p.instruments[size_t(instrument)];
    if (ins.instrumentType != songcore::InstrumentType::SAMPLER || !ins.sampleFilePath) return -1;
    return studio::uri_code(ins.sampleFilePath->c_str());
}

std::array<int, 3> macros_of(int code) {
    if (code & studio::kCustomFlag)
        return {studio::custom_macro(code, 0) * 2, studio::custom_macro(code, 1) * 2, studio::custom_macro(code, 2) * 2};
    return studio::preset_macros(code);
}

// "1-2", "5-8", "1,3,6" — the tracks of a mute mask, 1-based.
std::string track_list(int mask) {
    std::string out;
    for (int t = 0; t < 8;) {
        if (!(mask >> t & 1)) { ++t; continue; }
        int end = t;
        while (end + 1 < 8 && (mask >> (end + 1) & 1)) ++end;
        if (!out.empty()) out += ",";
        out += std::to_string(t + 1);
        if (end > t) out += "-" + std::to_string(end + 1);
        t = end + 1;
    }
    return out.empty() ? std::string("none") : out;
}

std::string upper(std::string s) {
    for (char& ch : s) ch = char(std::toupper(static_cast<unsigned char>(ch)));
    return s;
}

// The next synth instrument after `from` in `direction`, through −1 (none).
int next_synth(const songcore::Project& p, int from, int direction) {
    const int count = int(std::min<size_t>(p.instruments.size(), songcore::POOL_INSTRUMENTS));
    int id = from;
    for (int step = 0; step <= count; ++step) {
        id += direction;
        if (id >= count) id = -1;
        if (id < -1) id = count - 1;
        if (id == -1 || synth_code(p, id) >= 0) return id;
    }
    return -1;
}
}  // namespace

bool perform_group_muted(const songcore::Project& p, int slot) {
    if (slot < 0 || slot >= songcore::PERFORM_SLOTS) return false;
    const auto& s = p.perform[size_t(slot)];
    if (s.type != Type::MUTE || (s.tracks & 0xFF) == 0) return false;
    for (int t = 0; t < 8 && t < int(p.tracks.size()); ++t)
        if ((s.tracks >> t & 1) && !p.tracks[size_t(t)].mute) return false;
    return true;
}

int perform_slot_value(const songcore::Project& p, int filter, int slot) {
    if (slot < 0 || slot >= songcore::PERFORM_SLOTS) return -1;
    const auto& s = p.perform[size_t(slot)];
    if (s.type == Type::FILTER) return std::clamp(filter, 0, 255);
    if (s.type != Type::MACRO) return -1;
    const int code = synth_code(p, s.instrument);
    if (code < 0 || s.macro >= studio::macro_count(studio::preset_model(code))) return -1;
    return macros_of(code)[size_t(s.macro)];
}

std::string perform_slot_title(const songcore::Project& p, int slot) {
    if (slot < 0 || slot >= songcore::PERFORM_SLOTS) return {};
    const auto& s = p.perform[size_t(slot)];
    switch (s.type) {
        case Type::MUTE: return "MUTE " + track_list(s.tracks);
        case Type::FILTER: return "FILTER";
        case Type::MACRO: {
            const int code = synth_code(p, s.instrument);
            if (code < 0) return "MACRO";
            const int model = studio::preset_model(code);
            if (s.macro >= studio::macro_count(model)) return "MACRO " + std::string(1, char('A' + s.macro));
            return upper(studio::macro_name(model, s.macro));
        }
        case Type::EMPTY: break;
    }
    return "EMPTY";
}

std::string perform_slot_detail(const songcore::Project& p, int filter, int slot) {
    if (slot < 0 || slot >= songcore::PERFORM_SLOTS) return {};
    const auto& s = p.perform[size_t(slot)];
    char text[48];
    switch (s.type) {
        case Type::MUTE:
            if ((s.tracks & 0xFF) == 0) return "no tracks yet";
            return perform_group_muted(p, slot) ? "MUTED" : "playing";
        case Type::FILTER:
            if (filter == 128) return "OFF";
            std::snprintf(text, sizeof text, filter < 128 ? "LP %d %%" : "HP %d %%",
                          filter < 128 ? (128 - filter) * 100 / 128 : (filter - 128) * 100 / 127);
            return text;
        case Type::MACRO: {
            if (s.instrument < 0) return "B+up/down: synth";
            const int code = synth_code(p, s.instrument);
            if (code < 0) return "not a synth";
            std::snprintf(text, sizeof text, "%02X %s", s.instrument, studio::model_name(studio::preset_model(code)));
            return text;
        }
        case Type::EMPTY: break;
    }
    return "B+left/right: kind";
}

int perform_load_level(int load) { return load >= 900 ? 2 : load >= 700 ? 1 : 0; }

PerformResult perform_action(AppState& s, songcore::SongcoreHost& host, PerformAction action) {
    PerformResult r;
    auto& state = s.perform;
    const int slotIndex = std::clamp(state.cursor, 0, songcore::PERFORM_SLOTS - 1);
    songcore::Project& p = host.edit_project();
    PerformSlot& slot = p.perform[size_t(slotIndex)];
    const int filter = host.perform_filter();

    // Setup: the tile's kind, its instrument, its tracks or macro.
    if (action == PerformAction::KIND_NEXT || action == PerformAction::KIND_PREVIOUS) {
        const int step = action == PerformAction::KIND_NEXT ? 1 : 3;
        const Type next = Type((int(slot.type) + step) % 4);
        PerformSlot fresh;
        fresh.type = next;
        if (next == Type::MUTE) fresh.tracks = 1 << std::clamp(slotIndex, 0, 7);
        if (next == Type::MACRO) fresh.instrument = next_synth(p, -1, 1);
        slot = fresh;
        r.handled = r.edited = true;
        r.status = "TILE " + std::to_string(slotIndex + 1) + ": " + perform_slot_title(p, slotIndex);
        return r;
    }
    if (action == PerformAction::INSTRUMENT_NEXT || action == PerformAction::INSTRUMENT_PREVIOUS) {
        if (slot.type != Type::MACRO) { r.handled = true; r.status = "B+UP/DOWN PICKS A SYNTH ON A MACRO TILE"; return r; }
        slot.instrument = next_synth(p, slot.instrument, action == PerformAction::INSTRUMENT_NEXT ? 1 : -1);
        slot.macro = std::min(slot.macro, std::max(0, studio::macro_count(studio::preset_model(std::max(0, synth_code(p, slot.instrument)))) - 1));
        r.handled = r.edited = true;
        r.status = slot.instrument < 0 ? std::string("NO SYNTH IN THE POOL") : perform_slot_detail(p, filter, slotIndex);
        return r;
    }
    if (action == PerformAction::SUB_LEFT || action == PerformAction::SUB_RIGHT) {
        const int dir = action == PerformAction::SUB_RIGHT ? 1 : -1;
        r.handled = true;
        if (slot.type == Type::MUTE) {
            state.trackCursor = (state.trackCursor + dir + 8) % 8;
            r.status = "TRACK " + std::to_string(state.trackCursor + 1) + ((slot.tracks >> state.trackCursor & 1) ? " IN THE GROUP" : " NOT IN THE GROUP");
        } else if (slot.type == Type::MACRO) {
            const int code = synth_code(p, slot.instrument);
            const int count = code < 0 ? 3 : std::max(1, studio::macro_count(studio::preset_model(code)));
            slot.macro = (slot.macro + dir + count) % count;
            r.edited = true;
            r.status = perform_slot_title(p, slotIndex);
        }
        return r;
    }
    if (action == PerformAction::TRACK_TOGGLE) {
        r.handled = true;
        if (slot.type != Type::MUTE) return r;
        slot.tracks ^= 1 << state.trackCursor;
        r.edited = true;
        r.status = perform_slot_title(p, slotIndex);
        return r;
    }

    // Playing.
    switch (slot.type) {
        case Type::EMPTY:
            r.handled = true;
            r.status = "EMPTY TILE: B+LEFT/RIGHT GIVES IT A KIND";
            return r;
        case Type::MUTE: {
            if (action != PerformAction::TAP && action != PerformAction::VALUE_UP && action != PerformAction::VALUE_DOWN) {
                r.handled = true;
                return r;
            }
            if ((slot.tracks & 0xFF) == 0) { r.handled = true; r.status = "ADD TRACKS WITH L+LEFT/RIGHT AND L+A"; return r; }
            const bool mute = action == PerformAction::VALUE_UP ? true
                            : action == PerformAction::VALUE_DOWN ? false
                            : !perform_group_muted(p, slotIndex);
            for (int t = 0; t < 8 && t < int(p.tracks.size()); ++t)
                if (slot.tracks >> t & 1) p.tracks[size_t(t)].mute = mute;
            host.push_globals();   // a mute is not an edit: no dirty, no autosave (as R+B on SONG)
            r.handled = true;
            r.status = perform_slot_title(p, slotIndex) + (mute ? ": MUTED" : ": PLAYING");
            return r;
        }
        case Type::FILTER: {
            int value = filter;
            if (action == PerformAction::TAP) value = 128;
            else if (action == PerformAction::VALUE_UP) value += 16;
            else if (action == PerformAction::VALUE_DOWN) value -= 16;
            else if (action == PerformAction::VALUE_RIGHT) value += 4;
            else if (action == PerformAction::VALUE_LEFT) value -= 4;
            // Crossing the centre stops there once, so OFF is easy to find by ear and by hand.
            if ((filter < 128 && value > 128) || (filter > 128 && value < 128)) value = 128;
            value = std::clamp(value, 0, 255);
            host.set_perform_filter(value);
            state.filter = value;
            r.handled = true;
            r.status = "FILTER " + perform_slot_detail(p, value, slotIndex);
            return r;
        }
        case Type::MACRO: {
            r.handled = true;
            if (action == PerformAction::TAP) return r;
            const int code = synth_code(p, slot.instrument);
            if (code < 0) { r.status = slot.instrument < 0 ? "PICK A SYNTH WITH B+UP/DOWN" : "THIS INSTRUMENT IS NOT A SYNTH"; return r; }
            const int model = studio::preset_model(code);
            if (slot.macro >= studio::macro_count(model)) { r.status = "THIS SYNTH HAS NO SUCH MACRO"; return r; }
            auto m = macros_of(code);
            const int delta = action == PerformAction::VALUE_UP ? 16 : action == PerformAction::VALUE_DOWN ? -16
                            : action == PerformAction::VALUE_RIGHT ? 4 : -4;
            m[size_t(slot.macro)] = std::clamp(m[size_t(slot.macro)] + delta, 0, 255);
            const auto& ins = p.instruments[size_t(slot.instrument)];
            const std::string uri = studio::custom_uri(model, m[0], m[1], m[2]);
            if (!host.update_studio_synth(slot.instrument, uri, ins.studioSynthParams)) {
                r.status = "SYNTH NOT LOADED: OPEN IT IN STUDIO ONCE";
                return r;
            }
            r.edited = true;
            char text[48];
            std::snprintf(text, sizeof text, "%s %d %%", perform_slot_title(p, slotIndex).c_str(), m[size_t(slot.macro)] * 100 / 255);
            r.status = text;
            return r;
        }
    }
    return r;
}

void draw_perform(Canvas& c, int x, int y, const AppState& s) {
    const Theme& t = s.theme;
    const songcore::Project& p = *s.project;
    const auto& state = s.perform;
    constexpr int width = 499;

    c.surface_rect(x, y, width, 380, t.background);
    c.panel_rect(x, y, width, ROW_HEIGHT + 3, t.rowEvery4th, with_alpha(t.textParam, .18f), 4);
    c.draw_text("PERFORM", x + 10, y + TEXT_PADDING, t.textTitle, CHAR_SPACING, FONT_SCALE);
    {
        char cpu[48];
        std::snprintf(cpu, sizeof cpu, "CPU %d.%d %%  PEAK %d.%d %%", state.meanLoad / 10, state.meanLoad % 10,
                      state.worstLoad / 10, state.worstLoad % 10);
        const int level = perform_load_level(state.worstLoad);
        const Argb ink = level == 2 ? error_ink(t) : level == 1 ? t.textTitle : t.textParam;
        c.draw_label(cpu, x + width - 10 - Canvas::label_width(cpu, 12), y + 5, ink, 12);
    }

    // ── The eight tiles ─────────────────────────────────────────────────────────────────────────
    constexpr int gap = 6, tileW = (width - 3 * gap) / 4, tileH = 92;
    const int tilesY = y + ROW_HEIGHT + 10;
    for (int i = 0; i < songcore::PERFORM_SLOTS; ++i) {
        const int tx = x + (i % 4) * (tileW + gap), ty = tilesY + (i / 4) * (tileH + gap);
        const bool focus = i == state.cursor;
        const auto& slot = p.perform[size_t(i)];
        const bool mutedGroup = slot.type == songcore::PerformSlot::Type::MUTE && perform_group_muted(p, i);
        const Argb base = mutedGroup ? premium::mix(t.rowEvery4th, error_ink(t), .22f) : t.rowEvery4th;
        if (focus) c.mark_cursor(tx, ty, tileW, tileH);
        c.panel_rect(tx, ty, tileW, tileH, focus ? premium::mix(base, t.rowCursor, .14f) : base,
                     focus ? t.rowCursor : with_alpha(t.textParam, .22f), 5);
        c.draw_label(std::to_string(i + 1), tx + 8, ty + 6, with_alpha(t.textParam, .7f), 12);
        const std::string title = Canvas::clip_label(perform_slot_title(p, i), tileW - 30, 12);
        c.draw_label(title, tx + 24, ty + 6, focus ? t.rowCursor : t.textValue, 12);
        const std::string detail = Canvas::clip_label(perform_slot_detail(p, state.filter, i), tileW - 16, 12);
        c.draw_label(detail, tx + 8, ty + 26, mutedGroup ? error_ink(t) : t.textParam, 12);

        // A macro bar eases to a new value (Canvas::tween); the percent text shows it at once.
        const int raw = perform_slot_value(p, state.filter, i);
        const int value = raw >= 0 ? c.tween(0x5000000u + static_cast<uint32_t>(i), raw) : raw;
        const int barX = tx + 8, barY = ty + tileH - 22, barW = tileW - 16, barH = 10;
        if (slot.type == songcore::PerformSlot::Type::FILTER) {
            // Bipolar: the centre mark is OFF; the bar grows left (LP) or right (HP).
            c.fill_rect(barX, barY, barW, barH, with_alpha(t.textParam, .14f));
            const int mid = barX + barW / 2;
            c.fill_rect(mid, barY - 2, 1, barH + 4, with_alpha(t.textParam, .6f));
            const int reach = (value - 128) * (barW / 2) / 128;
            if (reach < 0) c.fill_rect(mid + reach, barY, -reach, barH, t.rowCursor);
            if (reach > 0) c.fill_rect(mid + 1, barY, reach, barH, t.rowCursor);
        } else if (value >= 0) {
            c.fill_rect(barX, barY, barW, barH, with_alpha(t.textParam, .14f));
            c.fill_rect(barX, barY, value * barW / 255, barH, focus ? t.rowCursor : with_alpha(t.rowCursor, .7f));
        } else if (slot.type == songcore::PerformSlot::Type::MUTE) {
            // The group's tracks as eight dots; the setup cursor under the focused tile.
            for (int tr = 0; tr < 8; ++tr) {
                const int dx = barX + tr * (barW / 8);
                const bool in = slot.tracks >> tr & 1;
                const bool trackMuted = tr < int(p.tracks.size()) && p.tracks[size_t(tr)].mute;
                c.fill_rect(dx, barY, barW / 8 - 3, barH, in ? (trackMuted ? error_ink(t) : t.rowCursor) : with_alpha(t.textParam, .14f));
                if (focus && tr == state.trackCursor) c.fill_rect(dx, barY + barH + 2, barW / 8 - 3, 2, t.textTitle);
            }
        }
    }

    // ── CPU per track ───────────────────────────────────────────────────────────────────────────
    const int cpuY = tilesY + 2 * tileH + gap + 10;
    const int cpuH = 380 - (cpuY - y) - 30;
    c.panel_rect(x, cpuY, width, cpuH, recess(t.background,t.rowEvery4th, .8f), with_alpha(t.textParam, .18f), 5);
    c.draw_label("CPU PER TRACK  (share of one core)", x + 10, cpuY + 6, t.rowCursor, 12);
    const int64_t drops = state.lateAtOpen >= 0 ? state.lateBlocks - state.lateAtOpen : 0;
    if (drops > 0) {
        const std::string text = "DROPOUTS " + std::to_string(drops);
        c.draw_label(text, x + width - 10 - Canvas::label_width(text, 12), cpuY + 6, error_ink(t), 12);
    } else {
        c.draw_label("no dropouts", x + width - 10 - Canvas::label_width("no dropouts", 12), cpuY + 6, t.textParam, 12);
    }
    static constexpr const char* names[10] = {"1", "2", "3", "4", "5", "6", "7", "8", "PRV", "MIX"};
    const int barsTop = cpuY + 26, barsH = cpuH - 50, slotW = (width - 20) / 10;
    // A full bar is a third of the core: one engine that heavy leaves little room for the rest.
    constexpr int fullScale = 333;
    for (int lane = 0; lane < 10; ++lane) {
        const int bx = x + 10 + lane * slotW, load = state.laneLoad[lane];
        const int h = std::min(barsH, load * barsH / fullScale);
        const int level = load >= 250 ? 2 : load >= 150 ? 1 : 0;
        const Argb ink = level == 2 ? error_ink(t) : level == 1 ? t.textTitle : t.rowCursor;
        c.fill_rect(bx + 6, barsTop, slotW - 14, barsH, with_alpha(t.textParam, .08f));
        if (h > 0) c.fill_rect(bx + 6, barsTop + barsH - h, slotW - 14, h, ink);
        char value[16];
        std::snprintf(value, sizeof value, "%d.%d", load / 10, load % 10);
        c.draw_label(value, bx + (slotW - Canvas::label_width(value, 12)) / 2, barsTop + barsH + 3, t.textParam, 12);
        const bool muted = lane < 8 && lane < int(p.tracks.size()) && p.tracks[size_t(lane)].mute;
        c.draw_label(names[lane], bx + (slotW - Canvas::label_width(names[lane], 12)) / 2, barsTop + barsH + 17,
                     muted ? error_ink(t) : t.textValue, 12);
    }

    c.draw_label("A+arrows: play  B+arrows: kind/synth  L+left/right, L+A: tracks/macro",
                 x + 2, y + 380 - 18, t.textParam, 12);
}

}  // namespace pt::ui
