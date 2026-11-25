#include "ui/drumlab.h"
#include "ui/drumlab_map.h"
#include "audio-engine.h"
#include "songcore/host.h"
#include "songcore/effects.h"
#include "songcore/timing.h"
#include "songcore/voice_derive.h"
#include "songcore/scales.h"
#include "ui/canvas.h"
#include "ui/helpers.h"
#include <algorithm>
#include <cstdio>
#include <iterator>
#include <limits>
#include <vector>

namespace pt::ui {
namespace {
constexpr const char* genres[] = {"Hip Hop", "Boom Bap", "Lo-Fi", "Drum & Bass", "House", "Techno", "Dub", "Breakbeat", "Euclidean", "Map"};
constexpr int genreCount = static_cast<int>(std::size(genres));
constexpr const char* roles[] = {"Kick", "Snare", "Closed Hat", "Open Hat", "Perc 1", "Perc 2", "Tom", "FX"};
constexpr const char* shortRoles[] = {"KICK", "SNR", "CH", "OH", "P1", "P2", "TOM", "FX"};
constexpr const char* settings[] = {"Style", "Sources", "Length", "Density", "Variation", "Swing", "Humanize", "Fill", "Seed"};
// Map style: rows 3, 4 and 7 become the map position and Chaos (density lives on each lane).
constexpr const char* mapSettings[] = {"Style", "Sources", "Length", "Map X", "Map Y", "Swing", "Humanize", "Chaos", "Seed"};
constexpr int euclideanSettings[] = {0, 1, 2, 5, 6, 8};
constexpr int laneVelocity[] = {108, 112, 84, 90, 87, 82, 99, 90};

enum LaneColumn { SLOT, LOCK, PERIOD, HITS, ROTATE };

bool euclidean(const DrumLabState& s) { return s.genre == DRUMLAB_EUCLIDEAN; }
bool map_style(const DrumLabState& s) { return s.genre == DRUMLAB_MAP; }
const char* setting_label(const DrumLabState& s, int row) { return (map_style(s) ? mapSettings : settings)[std::clamp(row, 0, 8)]; }

bool hidden_setting(const DrumLabState& s, int row) {
    return euclidean(s) && (row == 3 || row == 4 || row == 7);
}

bool euclidean_hit(const DrumLabLane& lane, int step) {
    // A modular accumulator spaces hits by floor(period/hits) or ceil(period/hits),
    // including the wrap. Subtracting rotation moves each hit later in the cycle.
    const int position = (step + lane.period - lane.rotation) % lane.period;
    return (position * lane.hits) % lane.period < lane.hits;
}

void bound_euclidean_lane(DrumLabLane& lane, int steps) {
    lane.period = std::clamp(lane.period, 1, steps);
    lane.hits = std::clamp(lane.hits, 0, lane.period);
    lane.rotation = std::clamp(lane.rotation, 0, lane.period - 1);
}

constexpr uint16_t bit(int n) { return static_cast<uint16_t>(1u << n); }
// Original rhythmic recipes. They are not transcriptions of songs or preset banks.
constexpr uint16_t masks[8][8] = {
    {bit(0)|bit(6)|bit(8), bit(4)|bit(12), 0x5555, bit(14), bit(3)|bit(11), bit(7)|bit(15), bit(15), bit(0)},
    {bit(0)|bit(7)|bit(10), bit(4)|bit(12), 0x5555, bit(6)|bit(14), bit(2)|bit(10), bit(13), bit(15), bit(0)},
    {bit(0)|bit(9), bit(4)|bit(12), 0x5555, bit(14), bit(3)|bit(11), bit(7), bit(15), bit(0)},
    {bit(0)|bit(10), bit(4)|bit(12), 0xffff, bit(6)|bit(14), bit(3)|bit(11), bit(7)|bit(15), bit(14)|bit(15), bit(0)},
    {0x1111, bit(4)|bit(12), 0x5555, 0x4444, bit(3)|bit(7)|bit(11)|bit(15), bit(6)|bit(14), bit(15), bit(0)},
    {0x1111, bit(4)|bit(12), 0xaaaa, 0x4444, bit(2)|bit(6)|bit(10)|bit(14), bit(3)|bit(9)|bit(15), bit(14), bit(0)},
    {bit(0)|bit(8), bit(8), 0x5555, bit(14), bit(6)|bit(14), bit(3)|bit(11), bit(15), bit(0)},
    {bit(0)|bit(6)|bit(10), bit(4)|bit(12), 0xaaaa, bit(14), bit(3)|bit(11), bit(7)|bit(15), bit(13)|bit(15), bit(0)}
};

struct Random {
    uint32_t state;
    explicit Random(uint32_t seed) : state(seed ? seed : 0x6d2b79f5u) {}
    uint32_t next() { state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state; }
    int below(int limit) { return limit > 0 ? static_cast<int>(next() % static_cast<unsigned>(limit)) : 0; }
};

bool valid_config(const DrumLabState& s) {
    if (s.genre < 0 || s.genre >= genreCount || s.activeLanes < 1 || s.activeLanes > 8 ||
        (s.steps != 16 && s.steps != 32 && s.steps != 64)) return false;
    for (int value : {s.density, s.variation, s.swing, s.humanize, s.fill, s.mapX, s.mapY, s.chaos})
        if (value < 0 || value > 100) return false;
    if (map_style(s)) for (int i = 0; i < s.activeLanes; ++i)
        if (s.lanes[i].mapDensity < 0 || s.lanes[i].mapDensity > 100) return false;
    if (euclidean(s)) for (int i = 0; i < s.activeLanes; ++i) {
        const auto& lane = s.lanes[i];
        if (lane.period < 1 || lane.period > s.steps || lane.hits < 0 || lane.hits > lane.period ||
            lane.rotation < 0 || lane.rotation >= lane.period) return false;
    }
    return true;
}

bool grid_current(const DrumLabState& s) {
    return s.generated && !s.dirty && s.generatedSteps == s.steps;
}

bool source_assigned(const songcore::Project& p, int id) {
    if (id < 0 || id >= static_cast<int>(p.instruments.size()) || id >= songcore::POOL_INSTRUMENTS) return false;
    const auto& ins = p.instruments[id];
    if (ins.instrumentType == songcore::InstrumentType::EXTERNAL) return false;
    return ins.instrumentType == songcore::InstrumentType::SOUNDFONT
               ? ins.soundfontPath.has_value() && !ins.soundfontPath->empty()
               : ins.sampleFilePath.has_value() && !ins.sampleFilePath->empty();
}

int straight_groove(const songcore::Project& p) {
    for (int id = 0; id < std::min<int>(songcore::POOL_GROOVES, p.grooves.size()); ++id) {
        const auto& groove = p.grooves[id];
        const int count = songcore::groove_active_length(groove), ruler = songcore::groove_ticks_per_step(groove);
        bool flat = true;
        for (int row = 0; row < count; ++row) if (groove.steps[row] != ruler) { flat = false; break; }
        if (flat) return id;
    }
    return -1;
}

bool ready(DrumLabState& s, const songcore::Project& p) {
    if (!valid_config(s)) { s.status = "Invalid Drum Lab settings; nothing changed."; return false; }
    if (p.scaleKey < 0 || p.scaleKey > 11) { s.status = "Set a valid project scale key before generating drums."; return false; }
    if (!grid_current(s)) { s.status = "Generate or Clear the pattern before Preview or Apply."; return false; }
    bool anySource = false;
    for (int lane = 0; lane < s.activeLanes; ++lane) {
        for (int step = 0; step < s.steps; ++step)
            if (s.grid[lane][step].velocity > 127 || s.grid[lane][step].delayTicks >= songcore::TICS_PER_STEP) {
                s.status = "Pattern values are invalid; regenerate before continuing."; return false;
            }
        if (!source_assigned(p, s.lanes[lane].instrument)) continue;   // an empty lane is silent
        anySource = true;
        const auto& root = p.instruments[s.lanes[lane].instrument].root;
        if (root.pitch < 0 || root.pitch > 11 || songcore::note_to_midi(root) < 0 || songcore::note_to_midi(root) > 131) {
            s.status = std::string("Set a valid ROOT note for ") + roles[lane] + "."; return false;
        }
    }
    if (!anySource) { s.status = "Assign a loaded sample / synth to at least one lane."; return false; }
    if (straight_groove(p) < 0) { s.status = "A straight groove slot is needed for matching Preview / Apply timing."; return false; }
    return true;
}

bool phrase_empty(const songcore::Phrase& phrase) {
    if (phrase.steps.size() != songcore::PHRASE_ROWS) return false;
    for (const auto& step : phrase.steps)
        if (step.note != songcore::Note::EMPTY() || step.instrument != 0 || step.volume != 0x7f ||
            step.fx1Type || step.fx1Value || step.fx2Type || step.fx2Value || step.fx3Type || step.fx3Value) return false;
    return true;
}

bool chain_empty(const songcore::Chain& chain) {
    return chain.phraseRefs.size() == songcore::CHAIN_ROWS && chain.transposeValues.size() == songcore::CHAIN_ROWS &&
           std::all_of(chain.phraseRefs.begin(), chain.phraseRefs.end(), [](int v) { return v == -1; }) &&
           std::all_of(chain.transposeValues.begin(), chain.transposeValues.end(), [](int v) { return v == 0; });
}

std::string hex(int value, int digits = 2) {
    char text[24]; std::snprintf(text, sizeof(text), digits == 8 ? "%08X" : "%02X", static_cast<unsigned>(value));
    return text;
}

std::string slot_text(int instrument) { return instrument < 0 ? "--" : hex(instrument); }

std::string setting_value(const DrumLabState& s, int row) {
    switch (row) {
        case 0: return drumlab_genre_name(s.genre);
        case 1: return std::to_string(s.activeLanes) + " lanes";
        case 2: return std::to_string(s.steps) + " steps";
        case 3: return std::to_string(map_style(s) ? s.mapX : s.density) + "%";
        case 4: return std::to_string(map_style(s) ? s.mapY : s.variation) + "%";
        case 5: return std::to_string(s.swing) + "%";
        case 6: return std::to_string(s.humanize) + "%";
        case 7: return std::to_string(map_style(s) ? s.chaos : s.fill) + "%";
        default: return hex(static_cast<int>(s.seed), 8);
    }
}

void schedule_hit(AudioEngine& engine, const songcore::Project& project, int track, int instrument,
                  const DrumLabStep& step, int64_t frame, int tempo) {
    engine.scheduleProgramNote(frame, track, instrument, drumlab_note_on(project, instrument, step), tempo, false);
}

// An assigned lane whose sound is loaded. Every other lane is silent and skipped.
bool lane_plays(const DrumLabState& s, songcore::SongcoreHost& host, int lane) {
    const int id = s.lanes[lane].instrument;
    return source_assigned(host.project(), id) && host.instrument_media_ready(id);
}

void stop_grid_audition(DrumLabState& s, songcore::SongcoreHost& host) {
    if (!s.gridAudition) return;
    s.gridAudition = false;
    host.stop_preview(true);
}

bool audition_grid_hit(DrumLabState& s, songcore::SongcoreHost& host, AudioEngine& engine,
                       int lane, const DrumLabStep& hit) {
    stop_grid_audition(s, host);
    if (!hit.velocity || s.previewRunning || host.is_playing()) return true;
    const auto& project = host.project();
    const int instrument = s.lanes[lane].instrument;
    if (!source_assigned(project, instrument) || !host.instrument_media_ready(instrument)) return false;
    const auto& root = project.instruments[instrument].root;
    if (songcore::note_is_pitch(root)) {
        host.preview_note(instrument, root, std::max(1, engine.getSampleRate() / 5), true);
        s.gridAudition = true;
    }
    return true;
}

void record_grid_edit(DrumLabState& s, int lane, int step, const DrumLabStep& next) {
    auto& history = s.stepHistory;
    if (s.stepHistoryCursor == DRUMLAB_STEP_HISTORY) {
        std::move(history.begin() + 1, history.end(), history.begin());
        --s.stepHistoryCursor;
    }
    auto& hit = s.grid[lane][step];
    history[s.stepHistoryCursor++] = {lane, step, hit, next};
    s.stepHistorySize = s.stepHistoryCursor;
    hit = next;
    s.unapplied = true;
}

// A+Up/Down: velocity in steps of 8 (1..127). A+Left/Right: timing nudge, 0..11 ticks.
void edit_grid_hit(DrumLabState& s, songcore::SongcoreHost& host, AudioEngine& engine, Button button) {
    const int lane = s.row - 9;
    if (s.lanes[lane].locked) { s.status = "Lane locked. Press B and unlock it to edit steps."; return; }
    DrumLabStep hit = s.grid[lane][s.gridStep];
    if (!hit.velocity) { s.status = "No hit on this step. Tap A to add one."; return; }
    const bool velocity = button == Button::DPAD_UP || button == Button::DPAD_DOWN;
    if (velocity)
        hit.velocity = static_cast<uint8_t>(std::clamp(hit.velocity + (button == Button::DPAD_UP ? 8 : -8), 1, 127));
    else
        hit.delayTicks = static_cast<uint8_t>(std::clamp(hit.delayTicks + (button == Button::DPAD_RIGHT ? 1 : -1),
                                                         0, songcore::TICS_PER_STEP - 1));
    if (hit == s.grid[lane][s.gridStep]) return;
    record_grid_edit(s, lane, s.gridStep, hit);
    s.confirmApply = false;
    s.status = velocity ? "Velocity " + std::to_string(hit.velocity)
                        : "Delay " + std::to_string(hit.delayTicks) + " ticks";
    if (velocity) audition_grid_hit(s, host, engine, lane, hit);
}

void apply_grid_history(DrumLabState& s, songcore::SongcoreHost& host, AudioEngine& engine, bool redo) {
    const char* action = redo ? "Redo" : "Undo";
    const int entry = s.stepHistoryCursor - (redo ? 0 : 1);
    if (entry < 0 || entry >= s.stepHistorySize) {
        s.status = redo ? "Nothing to redo." : "Nothing to undo.";
        return;
    }
    const auto& edit = s.stepHistory[entry];
    if (s.lanes[edit.lane].locked) {
        s.status = std::string("Unlock ") + roles[edit.lane] + " before " + action + ".";
        return;
    }
    auto& hit = s.grid[edit.lane][edit.step];
    hit = redo ? edit.after : edit.before;
    s.unapplied = true;
    s.stepHistoryCursor += redo ? 1 : -1;
    s.confirmApply = false;
    s.status = std::string(action) + " / " + roles[edit.lane] + " / step " + std::to_string(edit.step + 1);
    audition_grid_hit(s, host, engine, edit.lane, hit);
}

bool grid_button(const ButtonEvent& e, DrumLabState& s, songcore::SongcoreHost& host, AudioEngine& engine) {
    if (e.button == Button::B) {
        s.gridAHeld = s.gridPreviewHeld = false;
        if (e.action == ButtonAction::PRESSED || s.gridBUsed) s.gridBUsed = false;
        else {
            drumlab_clear_grid_input(s);
            stop_grid_audition(s, host);
            s.gridFocused = false;
            s.status = "Draft ready. Preview or Apply to Song.";
        }
        return true;
    }
    if (!valid_config(s) || !grid_current(s)) {
        drumlab_clear_grid_input(s);
        s.gridFocused = false;
        stop_grid_audition(s, host);
        s.status = "Generate or Clear before editing steps.";
        return true;
    }

    s.row = std::clamp(s.row, 9, 8 + s.activeLanes);
    s.gridStep = std::clamp(s.gridStep, 0, s.steps - 1);
    if (e.button == Button::DPAD_UP || e.button == Button::DPAD_DOWN) {
        const uint8_t direction = e.button == Button::DPAD_UP ? 1u : 2u;
        if (s.gridHistoryDirections & direction) {
            if (e.action == ButtonAction::RELEASED) s.gridHistoryDirections &= ~direction;
            return true;
        }
        if (e.action == ButtonAction::PRESSED && e.mods.b) {
            s.gridBUsed = true;
            s.gridAHeld = false;
            s.gridHistoryDirections |= direction;
            if (!e.mods.a) apply_grid_history(s, host, engine, e.button == Button::DPAD_DOWN);
            return true;
        }
    }
    if (e.button == Button::DPAD_DOWN && s.gridPreviewHeld) {
        if (e.action == ButtonAction::RELEASED) s.gridPreviewHeld = false;
        return true;
    }
    if (e.button == Button::A) {
        if (e.action == ButtonAction::PRESSED) {
            s.gridAHeld = !e.mods.b;
            return true;
        }
        if (!s.gridAHeld) return true;
        s.gridAHeld = false;
        if (e.mods.b) return true;
        s.confirmApply = false;
        const int lane = s.row - 9;
        if (s.lanes[lane].locked) {
            s.status = "Lane locked. Press B and unlock it to edit steps.";
            return true;
        }
        DrumLabStep hit{};
        if (!s.grid[lane][s.gridStep].velocity) {
            hit.velocity = static_cast<uint8_t>(laneVelocity[lane]);
            hit.delayTicks = static_cast<uint8_t>(s.gridStep % 2 ? s.swing * 6 / 100 : 0);
        }
        record_grid_edit(s, lane, s.gridStep, hit);
        s.status = hit.velocity ? "Hit added to the draft." : "Hit removed from the draft.";
        if (!audition_grid_hit(s, host, engine, lane, hit))
            s.status = "Hit added. Load the lane's instrument to hear it.";
        return true;
    }
    if (e.button == Button::L_SHIFT || e.button == Button::R_SHIFT) {
        s.gridAHeld = false;
        if (e.action == ButtonAction::RELEASED) {
            const int delta = e.button == Button::R_SHIFT ? 1 : -1;
            const int pages = s.steps / 16;
            s.gridStep = ((s.gridStep / 16 + delta + pages) % pages) * 16 + s.gridStep % 16;
        }
    } else if (is_dpad(e.button) && e.action == ButtonAction::PRESSED) {
        if (e.mods.l) {   // L+Down loops the preview; the other L+arrows (pages) do nothing here
            s.gridAHeld = false;
            if (e.button == Button::DPAD_DOWN) {
                drumlab_preview(s, host, engine);
                s.gridPreviewHeld = true;
            }
            return true;
        }
        if (e.mods.a) {
            s.gridAHeld = false;  // A's release must never also toggle the cell.
            edit_grid_hit(s, host, engine, e.button);
            return true;
        }
        if (e.button == Button::DPAD_LEFT || e.button == Button::DPAD_RIGHT)
            s.gridStep = std::clamp(s.gridStep + (e.button == Button::DPAD_RIGHT ? 1 : -1), 0, s.steps - 1);
        else
            s.row = std::clamp(s.row + (e.button == Button::DPAD_DOWN ? 1 : -1), 9, 8 + s.activeLanes);
    }
    s.gridPage = s.gridStep / 16;
    return true;
}

void draw_grid_detail(Canvas& c, const DrumLabState& s, const Theme& t, const songcore::Project& project) {
    const int lane = std::clamp(s.row - 9, 0, DRUMLAB_LANES - 1);
    const int step = std::clamp(s.gridStep, 0, DRUMLAB_MAX_STEPS - 1);
    const auto& hit = s.grid[lane][step];
    const auto& source = s.lanes[lane];
    const bool assigned = source_assigned(project, source.instrument);
    const auto sourceName = assigned ? project.instruments[source.instrument].name : "No source assigned";
    c.draw_label("STEP EDIT",29,168,t.rowCursor,12);
    c.draw_label(roles[lane],29,190,t.textTitle,18);
    c.draw_label(Canvas::clip_label(slot_text(source.instrument) + "  " + sourceName,184,12),29,218,t.textValue,12);
    c.draw_label("STEP " + std::to_string(step + 1),29,247,t.textValue,14);
    const char* state = hit.velocity ? "HIT" : "REST";
    c.panel_rect(144,243,66,24,hit.velocity?t.rowCursor:t.background,with_alpha(t.rowCursor,.75f),4);
    c.draw_label(state,177-Canvas::label_width(state,12)/2,248,hit.velocity?t.background:t.textValue,12);
    c.draw_label("VELOCITY",29,280,t.textParam,12);
    c.draw_label(hit.velocity?std::to_string(hit.velocity):"-",145,280,t.textValue,12);
    c.draw_label("DELAY",29,303,t.textParam,12);
    c.draw_label(hit.velocity?std::to_string(hit.delayTicks)+" ticks":"-",145,303,t.textValue,12);
    c.draw_label(source.locked?"LOCKED":"Lane unlocked",29,333,source.locked?t.textTitle:t.textParam,12);
    const int undo = s.stepHistoryCursor, redo = s.stepHistorySize - undo;
    const bool canUndo = undo > 0 && !s.lanes[s.stepHistory[undo - 1].lane].locked;
    const bool canRedo = redo > 0 && !s.lanes[s.stepHistory[undo].lane].locked;
    c.fill_rect(29,360,180,1,with_alpha(t.textParam,.35f));
    c.draw_label("UNDO " + std::to_string(undo),29,371,canUndo?t.textValue:t.textParam,12);
    c.draw_label("REDO " + std::to_string(redo),128,371,canRedo?t.textValue:t.textParam,12);
}
} // namespace

const char* drumlab_genre_name(int genre) { return genres[std::clamp(genre, 0, genreCount - 1)]; }
const char* drumlab_lane_name(int lane) { return roles[std::clamp(lane, 0, 7)]; }

songcore::NoteOnPayload drumlab_note_on(const songcore::Project& project, int instrument, const DrumLabStep& step) {
    songcore::NoteOnPayload result{};
    if (instrument < 0 || instrument >= static_cast<int>(project.instruments.size())) return result;
    const auto& ins = project.instruments[instrument];
    const int transpose = songcore::effective_transpose_semitones(songcore::project_transpose_semitones(project), project, instrument, {});
    int midi = songcore::note_to_midi(ins.root);
    if (transpose != 0) midi = std::clamp(midi + transpose, 0, 127);
    // Every generated phrase explicitly restores the base scale on its first row.
    // Ask the same helper the phrase note cursor uses, including sliced/TRANSP exemptions.
    static const songcore::Phrase baseScalePhrase;
    if (midi >= 0 && midi <= 127) midi = songcore::phrase_note_scale(project, baseScalePhrase, 0, instrument).snap(midi);
    result.note = static_cast<uint8_t>(midi);
    result.transpose = transpose;
    result.velocity = static_cast<int8_t>(step.velocity);
    const float velocity = step.velocity / 127.f;
    result.velGainBits = songcore::f32_bits(velocity * velocity);
    result.volGainBits = songcore::f32_bits(songcore::hex_to_float(ins.volume));
    result.panBits = songcore::f32_bits(songcore::hex_to_float(ins.pan));
    result.start = result.slice = result.tableId = result.tableRow = -1;
    return result;
}

int drumlab_hit_count(const DrumLabState& s) {
    int count = 0;
    for (int lane = 0; lane < std::clamp(s.activeLanes, 0, 8); ++lane)
        for (int step = 0; step < std::clamp(s.generatedSteps, 0, 64); ++step) count += s.grid[lane][step].velocity > 0;
    return count;
}

bool drumlab_generate(DrumLabState& s) {
    if (!valid_config(s)) { s.status = "Invalid Drum Lab settings; pattern kept."; return false; }
    if (s.previewRunning) { s.status = "Stop Preview before generating a new pattern."; return false; }
    auto generated = s.grid;
    const bool isEuclidean = euclidean(s);
    const bool isMap = map_style(s);
    for (int lane = 0; lane < 8; ++lane) {
        if (s.generated && s.lanes[lane].locked) continue;
        generated[lane] = {};
        if (lane >= s.activeLanes || s.lanes[lane].instrument == DRUMLAB_NO_SOURCE) continue;
        if (isMap) {
            // Map: the blended level of each step against the lane's density threshold. Chaos moves
            // every level by a seeded amount, differently in every bar, so longer patterns vary.
            Random random(s.seed ^ (0x9e3779b9u * static_cast<uint32_t>(lane + 1)) ^ 0x27d4eb2fu);
            const int density = s.lanes[lane].mapDensity, limit = drum_map::threshold(density);
            const int spread = s.chaos * 96 / 100;
            for (int step = 0; step < s.steps; ++step) {
                const int position = step % 16;
                const int level = drum_map::level(s.mapX, s.mapY, lane, position);
                const int jitter = random.below(2 * spread + 1) - spread;
                const int humanVelocity = random.below(2 * (s.humanize / 4) + 1) - s.humanize / 4;
                const int maxDelay = s.humanize ? 1 + (s.humanize - 1) * 3 / 100 : 0;
                const int humanDelay = random.below(maxDelay + 1);
                if (density == 0 || level + jitter <= limit) continue;
                // Core steps accented, ghost-level steps soft, the rest as the lane plays them.
                const int shade = level >= 200 ? 10 : level >= 140 ? 0 : level >= 80 ? -18 : -28;
                generated[lane][step].velocity = static_cast<uint8_t>(std::clamp(laneVelocity[lane] + shade + humanVelocity, 24, 127));
                generated[lane][step].delayTicks = static_cast<uint8_t>(std::clamp((step % 2 ? s.swing * 6 / 100 : 0) + humanDelay, 0, songcore::TICS_PER_STEP - 1));
            }
            continue;
        }
        Random random(s.seed ^ (0x9e3779b9u * static_cast<uint32_t>(lane + 1)) ^ (0x85ebca6bu * static_cast<uint32_t>(s.genre + 1)));
        for (int step = 0; step < s.steps; ++step) {
            const int position = step % 16;
            const bool base = isEuclidean ? euclidean_hit(s.lanes[lane], step) : (masks[s.genre][lane] & bit(position)) != 0;
            const bool anchor = lane < 2 && base;
            // The original styles retain exactly the same sequence of random draws.
            const int chance = isEuclidean ? 0 : random.below(100);
            const int fillChance = isEuclidean ? 0 : random.below(100);
            const int humanVelocity = random.below(2 * (s.humanize / 4) + 1) - s.humanize / 4;
            const int maxDelay = s.humanize ? 1 + (s.humanize - 1) * 3 / 100 : 0;
            const int humanDelay = random.below(maxDelay + 1);
            const int ghostProbability = s.density * s.variation / (lane == 2 ? 250 : 450);
            bool hit = isEuclidean ? base : anchor || chance < (base ? s.density : ghostProbability);
            if (!isEuclidean && lane >= 1 && lane != 3 && step >= s.steps - 4 && fillChance < s.fill &&
                (lane == 1 || lane == 6 || position % 2 == lane % 2)) hit = true;
            if (!hit) continue;
            const int accent = position % 4 == 0 ? 7 : (lane == 2 && position % 4 == 2 ? -13 : 0);
            const int ghost = !base && !anchor ? -25 : 0;
            generated[lane][step].velocity = static_cast<uint8_t>(std::clamp(laneVelocity[lane] + accent + ghost + humanVelocity, 24, 127));
            generated[lane][step].delayTicks = static_cast<uint8_t>(std::clamp((step % 2 ? s.swing * 6 / 100 : 0) + humanDelay, 0, songcore::TICS_PER_STEP - 1));
        }
    }
    s.grid = generated; s.generated = true; s.dirty = false; s.generatedSteps = s.steps; s.confirmApply = false;
    s.confirmClear = false;
    s.stepHistoryCursor = s.stepHistorySize = 0;
    s.gridPage = std::clamp(s.gridPage, 0, s.steps / 16 - 1);
    s.unapplied = drumlab_hit_count(s) > 0;
    s.status = std::to_string(drumlab_hit_count(s)) + " hits generated. Preview, then append safely with Apply.";
    return true;
}

bool drumlab_clear(DrumLabState& s, bool confirmed) {
    if (!valid_config(s)) { s.status = "Invalid Drum Lab settings; pattern kept."; return false; }
    if (s.previewRunning) { s.status = "Stop Preview before clearing the pattern."; return false; }
    s.confirmApply = false;
    bool losesHits = false;   // only unlocked lanes are emptied; locks keep theirs, as on Generate
    for (int lane = 0; lane < DRUMLAB_LANES && s.unapplied; ++lane)
        if (!(s.generated && s.lanes[lane].locked))
            for (const auto& hit : s.grid[lane]) losesHits |= hit.velocity > 0;
    if (losesHits && !confirmed) {
        s.confirmClear = true;
        s.status = "CLEAR PATTERN? A clears the unapplied hits, B keeps them.";
        return false;
    }
    for (int lane = 0; lane < DRUMLAB_LANES; ++lane)
        if (!(s.generated && s.lanes[lane].locked)) s.grid[lane] = {};
    s.generated = true; s.dirty = false; s.generatedSteps = s.steps; s.confirmClear = false;
    s.unapplied = false;
    s.stepHistoryCursor = s.stepHistorySize = 0;
    s.gridPage = std::clamp(s.gridPage, 0, s.steps / 16 - 1);
    s.status = "Pattern cleared. A on a lane's SLOT opens Step Edit.";
    return true;
}

bool drumlab_sources_page(const DrumLabState& s) {
    return s.row >= 17 ? s.footerOnSources : s.row >= 9;
}

bool drumlab_apply(DrumLabState& s, songcore::SongcoreHost& host) {
    if (host.is_playing() || s.previewRunning) { s.status = "Stop playback before Apply; song kept."; return false; }
    try {
    const auto& project = host.project();
    if (!ready(s, project)) return false;
    if (project.tracks.size() < 8) { s.status = "Project track bank is incomplete; song kept."; return false; }
    int last = -1;
    std::array<bool, songcore::POOL_CHAINS> usedChains{};
    std::array<bool, songcore::POOL_PHRASES> usedPhrases{};
    for (const auto& track : project.tracks) for (size_t row = 0; row < track.chainRefs.size(); ++row) {
        const int chain = track.chainRefs[row];
        if (chain != -1) last = std::max(last, static_cast<int>(std::min<size_t>(row, 256)));
        if (chain >= 0 && chain < songcore::POOL_CHAINS) usedChains[chain] = true;
    }
    if (last >= 255) { s.status = "No room to append: song row FF is occupied. Song kept."; return false; }
    for (const auto& chain : project.chains) for (int phrase : chain.phraseRefs)
        if (phrase >= 0 && phrase < songcore::POOL_PHRASES) usedPhrases[phrase] = true;
    std::vector<int> freeChains, freePhrases;
    for (int id = 0; id < std::min<int>(songcore::POOL_CHAINS, project.chains.size()); ++id)
        if (!usedChains[id] && project.chains[id].id == id && chain_empty(project.chains[id])) freeChains.push_back(id);
    for (int id = 0; id < std::min<int>(songcore::POOL_PHRASES, project.phrases.size()); ++id)
        if (!usedPhrases[id] && project.phrases[id].id == id && phrase_empty(project.phrases[id])) freePhrases.push_back(id);
    const int bars = s.steps / 16, groove = straight_groove(project);
    // A lane with no sound or no hits writes nothing and claims no chain or phrase.
    std::array<bool, DRUMLAB_LANES> writes{};
    int writing = 0;
    for (int lane = 0; lane < s.activeLanes; ++lane) {
        if (!source_assigned(project, s.lanes[lane].instrument)) continue;
        for (int step = 0; step < s.steps && !writes[lane]; ++step) writes[lane] = s.grid[lane][step].velocity > 0;
        writing += writes[lane];
    }
    if (!writing) { s.status = "Nothing to apply: no hits on a lane with a sound. Song kept."; return false; }
    if (static_cast<int>(freeChains.size()) < writing || static_cast<int>(freePhrases.size()) < writing * bars) {
        s.status = "Not enough empty, unused chains / phrases. Song kept."; return false;
    }
    // Finish every allocation before committing the three vector swaps below.
    auto tracks = project.tracks; auto chains = project.chains; auto phrases = project.phrases;
    const int row = last + 1;
    for (int lane = 0, claimed = 0; lane < 8; ++lane) {
        tracks[lane].chainRefs.resize(std::max<int>(tracks[lane].chainRefs.size(), row + 1), -1);
        if (!writes[lane]) continue;
        const int chainId = freeChains[claimed]; tracks[lane].chainRefs[row] = chainId;
        chains[chainId] = songcore::Chain(chainId);
        for (int bar = 0; bar < bars; ++bar) {
            const int phraseId = freePhrases[claimed * bars + bar];
            chains[chainId].phraseRefs[bar] = phraseId; phrases[phraseId] = songcore::Phrase(phraseId);
            for (int position = 0; position < 16; ++position) {
                auto& target = phrases[phraseId].steps[position];
                // Explicitly reset inherited song grooves, without editing any groove bank.
                if (position == 0) { target.fx2Type = songcore::FX_GRV; target.fx2Value = groove; }
                if (position == 0) { target.fx3Type = songcore::FX_SCA; target.fx3Value = std::clamp(project.scaleKey, 0, 11) << 4; }
                const auto& hit = s.grid[lane][bar * 16 + position];
                if (!hit.velocity) continue;
                target.note = project.instruments[s.lanes[lane].instrument].root;
                target.instrument = s.lanes[lane].instrument; target.volume = hit.velocity;
                if (hit.delayTicks) { target.fx1Type = songcore::FX_LAT; target.fx1Value = hit.delayTicks; }
            }
        }
        ++claimed;
    }
    std::string success = "Appended " + std::to_string(bars) + " bar(s) at SONG " + hex(row) + ". Existing music kept; save the project.";
    auto& target = host.edit_project();
    target.tracks.swap(tracks); target.chains.swap(chains); target.phrases.swap(phrases);
    s.appliedSongRow = row; ++s.applySerial; s.confirmApply = false; s.unapplied = false;
    s.status.swap(success);
    return true;
    } catch (const std::bad_alloc&) {
        s.status = "Not enough memory; song kept."; s.confirmApply = false; return false;
    }
}

bool drumlab_preview(DrumLabState& s, songcore::SongcoreHost& host, AudioEngine& engine) {
    if (s.previewRunning) { drumlab_stop(s, host, engine); s.status = "Preview stopped. Song unchanged."; return true; }
    if (host.is_playing()) { s.status = "Stop the song before Drum Lab Preview."; return false; }
    if (!ready(s, host.project())) return false;
    int silent = 0;   // lanes with no loaded sound are skipped, here and in drumlab_tick
    for (int lane = 0; lane < s.activeLanes; ++lane) silent += !lane_plays(s, host, lane);
    if (silent == s.activeLanes) { s.status = "No lane has a loaded sound. Reload it in INSTRUMENT."; return false; }
    stop_grid_audition(s, host);
    host.stop(); host.push_params();
    for (int table = 0; table < std::min<int>(songcore::POOL_TABLES, host.project().tables.size()); ++table)
        songcore::push_table(engine, host.project(), table);
    s.previewTempo = std::clamp(songcore::tempo_in_force(host.project()), 1, 999);
    s.previewFramesPerStep = std::max<int64_t>(1, songcore::frames_per_step(s.previewTempo, engine.getSampleRate()));
    s.previewStartFrame = engine.getCurrentFrame() + std::max(100, engine.getSampleRate() / 20);
    s.previewNextFrame = s.previewStartFrame; s.previewSwingRatio = 50;
    s.previewStepStartFrames.fill(-1);
    s.previewNextStep = 0; s.previewDisplayStep = -1; s.previewRunning = true; s.confirmApply = false;
    engine.requestResume();
    s.status = silent ? "Preview loops; lanes with no loaded sound stay silent. Song unchanged."
                      : "Preview loops on the existing mixer. A on Preview stops; song is unchanged.";
    drumlab_tick(s, host, engine); return true;
}

void drumlab_clear_grid_input(DrumLabState& s) {
    s.gridAHeld = s.gridPreviewHeld = false;
    s.gridBUsed = false;
    s.gridHistoryDirections = 0;
}

void drumlab_release_grid_audition(DrumLabState& s, songcore::SongcoreHost& host) {
    drumlab_clear_grid_input(s);
    stop_grid_audition(s, host);
}

void drumlab_stop(DrumLabState& s, songcore::SongcoreHost& host, AudioEngine& engine) {
    s.confirmApply = false;
    drumlab_release_grid_audition(s, host);
    if (!s.previewRunning) return;
    s.previewRunning = false; s.previewDisplayStep = -1;
    if (!host.is_playing()) { host.stop(); engine.clearScheduledNotes(); }
}

void drumlab_before_transport(DrumLabState& s, AudioEngine& engine) {
    const bool owned = s.previewRunning;
    drumlab_clear_grid_input(s);
    if (s.gridAudition) {
        s.gridAudition = false;
        const auto now = engine.getCurrentFrame();
        engine.clearScheduledNotesFrom(now, AudioEngine::PREVIEW_LANE);
        engine.scheduleCut(now, AudioEngine::PREVIEW_LANE);
    }
    s.previewRunning = false; s.confirmApply = false; s.previewDisplayStep = -1;
    if (owned) { engine.clearScheduledNotes(); engine.stopAllRamped(); }
}

void drumlab_tick(DrumLabState& s, songcore::SongcoreHost& host, AudioEngine& engine) {
    if (!s.previewRunning) return;
    if (host.is_playing()) { s.previewRunning = false; s.previewDisplayStep = -1; return; }
    if (!valid_config(s) || s.dirty || s.generatedSteps != s.steps ||
        songcore::tempo_in_force(host.project()) != s.previewTempo) {
        drumlab_stop(s, host, engine); s.status = "Settings changed; Preview stopped."; return;
    }
    const int64_t now = engine.getCurrentFrame(), horizon = now + std::max(512, engine.getSampleRate() / 8);
    // Do not spray stale hits if a modal operation paused UI scheduling for several seconds.
    if (now > s.previewNextFrame + engine.getSampleRate()) {
        // Jump to the current complete pair, then at most two half-pairs ahead. Never schedule a
        // burst of missed hits. At50 this is exactly the former ceil-to-the-next-step behavior.
        const int64_t pair = std::max<int64_t>(0, (now - s.previewStartFrame) / (2 * s.previewFramesPerStep));
        s.previewNextStep = pair * 2;
        s.previewNextFrame = s.previewStartFrame + pair * 2 * s.previewFramesPerStep;
        s.previewSwingRatio = std::clamp(host.project().swing, 50, 75);
        s.previewStepStartFrames.fill(-1);
        while (s.previewNextFrame < now) {
            s.previewStepStartFrames[static_cast<size_t>(s.previewNextStep % s.steps)] = s.previewNextFrame;
            s.previewNextFrame += songcore::swing_step_duration(s.previewFramesPerStep,
                                    s.previewSwingRatio, (s.previewNextStep & 1) != 0);
            ++s.previewNextStep;
        }
    }
    for (int budget = 0; budget < 64; ++budget) {
        const int64_t frame = s.previewNextFrame;
        if (frame > horizon) break;
        const int step = static_cast<int>(s.previewNextStep % s.steps);
        // Apply explicitly selects a straight groove, so Preview follows PROJECT Swing too, even
        // when groove0 is custom. Retain the ratio until its short step has completed the pair.
        if ((s.previewNextStep & 1) == 0) s.previewSwingRatio = std::clamp(host.project().swing, 50, 75);
        const int64_t stepDuration = songcore::swing_step_duration(s.previewFramesPerStep,
                                    s.previewSwingRatio, (s.previewNextStep & 1) != 0);
        for (int lane = 0; lane < s.activeLanes; ++lane) {
            if (!lane_plays(s, host, lane)) continue;   // also a sound unloaded while looping
            const auto& hit = s.grid[lane][step];
            if (hit.velocity) schedule_hit(engine, host.project(), lane, s.lanes[lane].instrument, hit,
                std::max(frame, now + 1) + hit.delayTicks * songcore::frames_per_tic(stepDuration), s.previewTempo);
        }
        s.previewStepStartFrames[static_cast<size_t>(step)] = frame;
        s.previewNextFrame += stepDuration;
        ++s.previewNextStep;
    }
    if (now >= s.previewStartFrame) {
        int64_t latest = -1;
        for (int step = 0; step < s.steps; ++step) {
            const int64_t frame = s.previewStepStartFrames[static_cast<size_t>(step)];
            if (frame >= s.previewStartFrame && frame <= now && frame > latest) {
                latest = frame; s.previewDisplayStep = step;
            }
        }
        if (!s.gridFocused && s.previewDisplayStep >= 0) s.gridPage = s.previewDisplayStep / 16;
    }
}

namespace {
// L+Left/Right: GROOVE <-> SOURCES, back on the row last used there. On the footer only the page
// shown above it changes.
void switch_page(DrumLabState& s) {
    const bool sources = drumlab_sources_page(s);
    s.confirmApply = s.confirmClear = false;
    if (s.row >= 17) { s.footerOnSources = !sources; return; }
    if (sources) { s.sourcesRow = s.row; s.row = std::clamp(s.grooveRow, 0, 8); }
    else { s.grooveRow = s.row; s.row = std::clamp(s.sourcesRow, 9, 16); }
    if (hidden_setting(s, s.row)) s.row = 0;
}

// L+A on a lane: open its instrument, with the sample browser when nothing is loaded there. A --
// lane first takes the first free instrument that no other lane points at.
void request_lane_sound(DrumLabState& s, songcore::SongcoreHost& host, AudioEngine& engine) {
    const auto& p = host.project();
    const int lane = s.row - 9;
    int id = s.lanes[lane].instrument;
    if (id == DRUMLAB_NO_SOURCE) {
        for (int candidate = 0; candidate < std::min<int>(128, p.instruments.size()) && id < 0; ++candidate) {
            bool taken = !songcore::instrument_is_free(p.instruments[candidate]);
            for (const auto& other : s.lanes) taken |= other.instrument == candidate;
            if (!taken) id = candidate;
        }
        if (id < 0) { s.status = "No free instrument left for this lane."; return; }
        s.lanes[lane].instrument = id;
    }
    if (id < 0 || id >= static_cast<int>(p.instruments.size())) return;
    drumlab_stop(s, host, engine);
    s.openInstrument = id;
    s.openBrowser = songcore::instrument_is_free(p.instruments[id]);
}
}  // namespace

bool drumlab_button(const ButtonEvent& e, DrumLabState& s, songcore::SongcoreHost& host, AudioEngine& engine) {
    const int lastLaneColumn = euclidean(s) ? ROTATE : map_style(s) ? PERIOD : LOCK;   // Map: PERIOD shows Density
    s.row = std::clamp(s.row, 0, DRUMLAB_ROWS - 1);
    s.col = std::clamp(s.col, 0, lastLaneColumn);
    s.footerAction = s.row >= 17 ? s.row - 17 : std::clamp(s.footerAction, 0, 3);
    if (e.button == Button::START) { if (e.action == ButtonAction::PRESSED) drumlab_stop(s, host, engine); return false; }
    if (e.button == Button::SELECT && e.action == ButtonAction::RELEASED) {
        drumlab_clear_grid_input(s);
        stop_grid_audition(s, host);
        s.showHelp = !s.showHelp;
        return true;
    }
    if (s.showHelp) {
        if (e.button == Button::B && e.action == ButtonAction::RELEASED) s.showHelp = false;
        return true;
    }
    if (s.gridFocused) return grid_button(e, s, host, engine);
    if (s.confirmClear && e.button == Button::B) {   // B keeps the pattern; the Studio routes it here
        if (e.action == ButtonAction::PRESSED) { s.confirmClear = false; s.status = "Pattern kept."; }
        return true;
    }
    if (e.button == Button::A && e.mods.l && e.action == ButtonAction::PRESSED) {   // L+A: the lane's sound
        s.aUsed = true;   // A's release must not also open Step Edit
        s.confirmApply = s.confirmClear = false;
        if (s.row >= 9 && s.row <= 16) request_lane_sound(s, host, engine);
        return true;
    }
    if (e.button == Button::A) {
        if (e.action == ButtonAction::PRESSED) { s.aUsed = false; return true; }
        if (s.aUsed) { s.aUsed = false; return true; }
        const bool clearConfirmed = s.confirmClear && s.row == 18;
        s.confirmClear = false;
        if (s.row == 17) { drumlab_stop(s, host, engine); if (s.generated) ++s.seed; drumlab_generate(s); }
        else if (s.row == 18) { drumlab_stop(s, host, engine); drumlab_clear(s, clearConfirmed); }
        else if (s.row == 19) drumlab_preview(s, host, engine);
        else if (s.row == 20) {
            const bool confirmed = s.confirmApply;
            drumlab_stop(s, host, engine);
            if (!confirmed) {
                if (ready(s, host.project())) { s.confirmApply = true; s.status = "A again: append to a new SONG row using only empty resources."; }
            } else drumlab_apply(s, host);
        } else if (s.row >= 9 && s.col == LOCK) { s.lanes[s.row - 9].locked = !s.lanes[s.row - 9].locked; s.confirmApply = false; }
        else if (s.row >= 9 && s.col == SLOT) {
            if (s.row - 9 >= s.activeLanes) s.status = "Choose an active lane to edit its steps.";
            else if (!valid_config(s)) s.status = "Invalid Drum Lab settings.";
            else if (!s.generated && !drumlab_clear(s)) {}   // never generated: start from an empty grid
            else if (!grid_current(s)) s.status = "Settings changed. Generate or Clear before editing steps.";
            else {
                s.gridFocused = true;
                drumlab_clear_grid_input(s);
                s.confirmApply = false;
                s.gridStep = std::clamp(s.gridPage, 0, s.steps / 16 - 1) * 16;
                s.status = "Draft steps. Use Apply to add them to the song.";
            }
        }
        return true;
    }
    if ((e.button == Button::L_SHIFT || e.button == Button::R_SHIFT) && e.action == ButtonAction::RELEASED) {
        const int pages = std::max(1, s.steps / 16), delta = e.button == Button::R_SHIFT ? 1 : -1;
        s.gridPage = (s.gridPage + pages + delta) % pages; return true;
    }
    if (e.action != ButtonAction::PRESSED || !is_dpad(e.button)) return false;
    const bool positive = e.button == Button::DPAD_RIGHT || e.button == Button::DPAD_UP;
    if (!e.mods.a) {
        if (e.mods.l) {   // L+Left/Right: the other page. L alone (released) still turns the bar.
            if (e.button == Button::DPAD_LEFT || e.button == Button::DPAD_RIGHT) switch_page(s);
            return true;
        }
        if (e.button == Button::DPAD_UP || e.button == Button::DPAD_DOWN) {
            // The four bottom actions share one visual row under either page. Vertical movement
            // stays on the page's own rows and enters / leaves that row; horizontal movement below
            // chooses an action in screen order.
            const bool sources = drumlab_sources_page(s);
            const int first = sources ? 9 : 0, last = sources ? 16 : 8;
            if (e.button == Button::DPAD_UP) s.row = s.row >= 17 ? last : std::max(first, s.row - 1);
            else if (s.row == last) { s.footerOnSources = sources; s.row = 17 + s.footerAction; }
            else if (s.row < 17) ++s.row;
            while (hidden_setting(s, s.row)) s.row += e.button == Button::DPAD_UP ? -1 : 1;
            s.confirmApply = s.confirmClear = false;
        } else if (s.row >= 17) {
            s.row = std::clamp(s.row + (positive ? 1 : -1), 17, 20);
            s.footerAction = s.row - 17; s.confirmApply = s.confirmClear = false;
        } else if (s.row >= 9 && s.row <= 16) s.col = std::clamp(s.col + (positive ? 1 : -1), 0, lastLaneColumn);
        return true;
    }
    s.aUsed = true; s.confirmApply = s.confirmClear = false;
    if (s.row >= 17 || hidden_setting(s, s.row)) return true;
    drumlab_stop(s, host, engine);
    const bool coarse = e.button == Button::DPAD_UP || e.button == Button::DPAD_DOWN;
    const int delta = (positive ? 1 : -1) * (coarse ? 10 : 1);
    if (s.row == 0) {
        s.genre = (s.genre + genreCount + (positive ? 1 : -1)) % genreCount;
        if (euclidean(s)) for (auto& lane : s.lanes) bound_euclidean_lane(lane, s.steps);
    }
    else if (s.row == 1) s.activeLanes = std::clamp(s.activeLanes + (positive ? 1 : -1), 1, 8);
    else if (s.row == 2) {
        s.steps = positive ? (s.steps < 32 ? 32 : 64) : (s.steps > 32 ? 32 : 16);
        if (euclidean(s)) for (auto& lane : s.lanes) bound_euclidean_lane(lane, s.steps);
    }
    else if (s.row >= 3 && s.row <= 7) {
        int* values[] = {&s.density, &s.variation, &s.swing, &s.humanize, &s.fill};
        if (map_style(s)) { values[0] = &s.mapX; values[1] = &s.mapY; values[4] = &s.chaos; }
        *values[s.row - 3] = std::clamp(*values[s.row - 3] + delta, 0, 100);
    } else if (s.row == 8) s.seed += static_cast<uint32_t>(delta);
    else if (s.row >= 9 && s.row <= 16) {
        auto& lane = s.lanes[s.row - 9];
        if (s.col == SLOT) {
            // Left of 00 is -- (no source): an empty, silent lane.
            lane.instrument = std::clamp(lane.instrument + (positive ? 1 : -1) * (coarse ? 16 : 1), DRUMLAB_NO_SOURCE, 127);
            return true;
        }
        if (s.col == LOCK) { lane.locked = !lane.locked; return true; }
        if (lane.locked) { s.status = map_style(s) ? "Unlock the lane to change its density." : "Unlock the lane to edit its Euclidean rhythm."; return true; }
        if (map_style(s)) {
            const int previous = lane.mapDensity;
            lane.mapDensity = std::clamp(lane.mapDensity + delta, 0, 100);
            if (lane.mapDensity == previous) return true;
            s.dirty = true; s.status = "Settings changed. Generate updates unlocked lanes."; return true;
        }
        const int change = (positive ? 1 : -1) * (coarse ? 4 : 1);
        const std::array previous{lane.period, lane.hits, lane.rotation};
        if (s.col == PERIOD) lane.period += change;
        if (s.col == HITS) lane.hits += change;
        if (s.col == ROTATE) {
            const int period = std::clamp(lane.period, 1, s.steps);
            lane.rotation = (lane.rotation + change % period + period) % period;
        }
        bound_euclidean_lane(lane, s.steps);
        if (std::array{lane.period, lane.hits, lane.rotation} == previous) return true;
    } else return true;
    s.dirty = true; s.status = "Settings changed. Generate updates unlocked lanes."; return true;
}

void drumlab_draw(Canvas& c, const DrumLabState& s, const Theme& t, const songcore::Project& project) {
    const Argb panel = t.rowEvery4th, accent = t.rowCursor, muted = t.textParam, ink = t.textValue;
    const bool onLane = s.row >= 9 && s.row <= 16;
    const bool assignments = drumlab_sources_page(s);   // the SOURCES page, else GROOVE
    const bool editGrid = s.gridFocused && onLane && !s.showHelp;
    const bool editRhythm = !editGrid && (euclidean(s) || map_style(s)) && onLane && !s.showHelp;
    c.panel_rect(18,104,604,44,panel,with_alpha(muted,.2f),6);
    c.draw_label("DRUM LAB",30,111,accent,12);
    if (editRhythm) {
        const int index = s.row - 9;
        const auto& lane = s.lanes[index];
        if (map_style(s)) {
            c.draw_label(std::string("Map / ") + roles[index],30,127,ink,12);
            const int x = 278;
            const bool selected = s.col == PERIOD;
            if (selected) c.panel_rect(x,109,107,34,with_alpha(accent,.14f),accent,4);
            c.draw_label("DENSITY",x+10,110,muted,12);
            c.draw_label(std::to_string(lane.mapDensity) + "%",x+10,126,selected?accent:ink,14);
            c.draw_label("Higher adds hits: core, then accents,",400,112,muted,12);
            c.draw_label("ghost notes, then the whole grid.",400,127,muted,12);
        } else {
        c.draw_label(std::string("Euclidean / ") + roles[index],30,127,ink,12);
        constexpr const char* fields[] = {"PERIOD", "HITS", "ROTATE"};
        const int values[] = {lane.period, lane.hits, lane.rotation};
        for (int i = 0; i < 3; ++i) {
            const int x = 278 + i * 112;
            const bool selected = s.col == PERIOD + i;
            if (selected) c.panel_rect(x,109,107,34,with_alpha(accent,.14f),accent,4);
            c.draw_label(fields[i],x+10,110,muted,12);
            c.draw_label(std::to_string(values[i]),x+10,126,selected?accent:ink,14);
        }
        }
    } else {
        c.draw_label(drumlab_genre_name(s.genre),30,127,ink,12);
        const std::string summary = std::to_string(std::clamp(s.activeLanes,1,8)) + " sources / " + std::to_string(s.steps) + " steps / " + std::to_string(drumlab_hit_count(s)) + " hits";
        c.draw_label(summary,260,120,muted,12);
        if (s.previewRunning) c.draw_label("LOOPING",540,120,t.textPlayhead,12);
        else if (map_style(s)) {
            // The map: 3 × 3 nodes, the position as a dot (straight → broken, sparse → busy upwards).
            const int mx = 556, my = 108, size = 36;
            c.panel_rect(mx,my,size,size,recess(t.background,panel,.7f),with_alpha(muted,.35f),3);
            for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j)
                c.fill_rect(mx+3+i*15,my+size-6-j*15,2,2,with_alpha(muted,.6f));
            const int px = mx+3+s.mapX*30/100, py = my+size-6-s.mapY*30/100;
            c.fill_rect(px-2,py-2,6,6,accent);
        }
    }
    if (s.showHelp) {
        c.panel_rect(18,158,604,282,panel,with_alpha(muted,.2f),7);
        const char* lines[] = {"L+Left/Right: GROOVE or SOURCES page. SLOT -- leaves a lane empty.",
            "A + arrows edits a setting. Up / Down chooses the next field.",
            euclidean(s)?"Left / Right: Slot, Lock, Period, Hits or Rotate on the selected lane.":
            map_style(s)?"Map X / Y moves through the rhythm map. Left / Right: Slot, Lock, Density.":
                         "Left / Right selects SLOT / LOCK or the bottom action buttons.",
            "Generate, or Clear to start empty. Locked lanes keep their hits.",
            "A on SLOT opens Step Edit. L+A on a lane loads or opens its sound.",
            "Step Edit: A+Up/Down velocity, A+Left/Right delay, L+Down loops.",
            "Apply needs A twice; only lanes with a sound and hits are written.",
            "B+Up undoes and B+Down redoes the last step edit.",
            "L / R changes the visible bar. Select or B closes this help."};
        for (int i=0;i<9;++i) c.draw_label(lines[i],30,173+i*27,i==6?accent:ink,12);
        return;
    }
    c.panel_rect(18,158,208,242,panel,with_alpha(muted,.18f),6);
    if (editGrid) draw_grid_detail(c,s,t,project);
    else {
        c.draw_label(assignments ? "SOURCES / LANE LOCKS" : "GROOVE SETTINGS",29,168,accent,12);
        c.draw_label(assignments ? "2/2" : "1/2",219-Canvas::label_width("1/2",12),168,muted,12);
    }
    const int settingsCount = euclidean(s) ? static_cast<int>(std::size(euclideanSettings)) : 9;
    for (int i=0;i<(editGrid?0:assignments?8:settingsCount);++i) {
        const int row = assignments ? i+9 : euclidean(s) ? euclideanSettings[i] : i;
        const int y = 190+i*(assignments?25:euclidean(s)?29:22);
        const bool selected = s.row==row;
        if(selected){c.panel_rect(25,y-2,194,23,recess(t.background,accent,.16f),accent,3);c.mark_cursor(25,y-2,194,23);}
        if(assignments){
            const bool active=i<std::clamp(s.activeLanes,1,8);
            c.draw_label(shortRoles[i],31,y+2,active?ink:muted,12);
            c.draw_text(slot_text(s.lanes[i].instrument),83,y+2,selected&&s.col==SLOT?accent:ink,1,2);
            const int id=s.lanes[i].instrument;
            std::string name=source_assigned(project,id)?project.instruments[id].name:id<0?"Empty":"No source";
            c.draw_label(Canvas::clip_label(name,60,12),111,y+2,muted,12);
            c.draw_label(s.lanes[i].locked?"LOCK":"free",180,y+2,selected&&s.col==LOCK?accent:muted,12);
        }else{
            c.draw_label(setting_label(s,row),31,y+2,selected?ink:muted,12);
            const std::string text=Canvas::clip_label(setting_value(s,row),109,12);
            c.draw_label(text,212-Canvas::label_width(text,12),y+2,selected?accent:ink,12);
        }
    }
    c.panel_rect(236,158,386,242,recess(t.background,panel,.78f),with_alpha(muted,.18f),6);
    const int pages=std::max(1,std::clamp(s.steps,16,64)/16), page=std::clamp(s.gridPage,0,pages-1);
    c.draw_label("STEP GRID",247,168,accent,12);
    c.draw_label("BAR "+std::to_string(page+1)+" / "+std::to_string(pages),520,168,muted,12);
    constexpr int gridX=296, gridY=193, cellW=19, cellH=24;
    for(int step=0;step<16;++step){
        if(step%4==0)c.draw_label(std::to_string(step/4+1),gridX+step*cellW+5,184,muted,12);
        if(s.previewRunning&&s.previewDisplayStep==page*16+step)c.fill_rect(gridX+step*cellW,201,18,192,with_alpha(accent,.13f));
    }
    for(int lane=0;lane<8;++lane){
        const bool active=lane<std::clamp(s.activeLanes,1,8);
        const bool focusedLane=editGrid&&s.row==lane+9;
        const int y=gridY+lane*cellH+12;
        if(focusedLane)c.fill_rect(244,y-1,348,18,with_alpha(accent,.09f));
        c.draw_label(shortRoles[lane],247,y+1,focusedLane?ink:active?muted:recess(t.background,muted,.4f),12);
        for(int step=0;step<16;++step){
            const auto& hit=s.grid[lane][page*16+step];
            const bool on=active&&s.generated&&hit.velocity>0;
            const bool focused=focusedLane&&s.gridStep==page*16+step;
            const int x=gridX+step*cellW;
            const Argb base=on?recess(t.background,accent,.38f+.62f*hit.velocity/127.f):with_alpha(muted,step%4==0?.15f:.07f);
            c.fill_rect(x+2,y+1,14,12,base);
            if(on&&hit.delayTicks)c.fill_rect(x+3,y+13,std::min(12,static_cast<int>(hit.delayTicks)),1,t.textTitle);
            if(focused){c.stroke_rect(x,y-2,18,19,accent,2);c.mark_cursor(x,y-2,18,19);}   // glides (ui/layout.cpp)
        }
    }
    if (editGrid) {
        c.panel_rect(18,410,604,30,panel,with_alpha(accent,.35f),5);
        c.draw_label("A Toggle   A+Up/Down Velocity   A+Left/Right Delay   L+Down Preview",29,412,ink,12);
        c.draw_label("L/R Bar   B Back   B+Up Undo   B+Down Redo   Select Help",29,426,ink,12);
        c.draw_label(Canvas::clip_label(s.status,604,12),18,449,muted,12);
        return;
    }
    const char* actions[]={"GENERATE","CLEAR","PREVIEW","APPLY TO SONG"};
    for(int i=0;i<4;++i){
        const bool selected=s.row==17+i; const int x=18+i*153, w=145;
        c.panel_rect(x,410,w,30,selected?accent:panel,selected?accent:with_alpha(muted,.23f),5);
        std::string text=i==1&&s.confirmClear?"CLEAR PATTERN?":i==2&&s.previewRunning?"STOP PREVIEW":
                         i==3&&s.confirmApply?"A AGAIN TO APPEND":actions[i];
        c.draw_label(text,x+(w-Canvas::label_width(text,12))/2,418,selected?t.background:ink,12);
    }
    c.draw_label(Canvas::clip_label(s.status,604,12),18,449,s.dirty||s.confirmClear?t.textTitle:muted,12);
    const char* footer = onLane&&s.col==SLOT?"A: Step Edit  A+arrows: Slot  L+A: Sound  L+Left/Right: Page  Select: Help":
                         editRhythm?"Left / Right: Field   A + arrows: Edit   L+Left/Right: Page   Select: Help":
                                    "A + arrows: Edit   L / R: Bar   L+Left/Right: Page   Select: Help";
    c.draw_label(footer,18,464,muted,12);
}
} // namespace pt::ui
