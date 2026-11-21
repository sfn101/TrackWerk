// SPDX-License-Identifier: GPL-3.0-or-later
// The song as a Standard MIDI File (format 1, 480 ticks per quarter): one tempo track and one track per
// audible tracker track, each on its own channel. Pure: no I/O, no engine.
//
// What is written: the notes of each track's first block of song rows (the rows that would play from
// the top of the song), chain by chain and phrase by phrase, with the chain's and the project's
// transpose, the step's volume as velocity (1..127), OFF steps as note-offs, and the project swing.
// A tracker track is monophonic, so a new note ends the one before it.
// What is NOT written (the report says how many steps had any): the effect commands, grooves, HOP,
// retrigs, table and modulation movement, and the instrument's own sound. It is a note sketch for a
// DAW, not a copy of the render.
#ifndef TRACKWERK_MIDI_EXPORT_H
#define TRACKWERK_MIDI_EXPORT_H

#include "effects.h"   // step_plays_on_pass (NTH, PRB)
#include "model.h"
#include "render.h"   // find_song_bounds
#include "timing.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace songcore {

struct MidiExportReport {
    int tracks = 0;          // tracker tracks that wrote notes
    int notes = 0;
    int stepsWithEffects = 0;   // steps whose effect commands are not in the file
};

namespace detail {

constexpr int kMidiPpq = 480;
constexpr int kMidiStepTicks = kMidiPpq / 4;   // a sixteenth

inline void put_u16(std::string& out, int v) { out.push_back(char((v >> 8) & 255)); out.push_back(char(v & 255)); }
inline void put_u32(std::string& out, uint32_t v) { for (int s = 24; s >= 0; s -= 8) out.push_back(char((v >> s) & 255)); }
inline void put_vlq(std::string& out, uint32_t v) {
    char buf[5];
    int n = 0;
    buf[n++] = char(v & 0x7F);
    while ((v >>= 7) > 0) buf[n++] = char(0x80 | (v & 0x7F));
    while (n > 0) out.push_back(buf[--n]);
}
inline void put_meta_text(std::string& out, uint32_t delta, int type, const std::string& text) {
    put_vlq(out, delta);
    out.push_back(char(0xFF));
    out.push_back(char(type));
    put_vlq(out, static_cast<uint32_t>(text.size()));
    out += text;
}
inline std::string mtrk(const std::string& body) {
    std::string chunk = "MTrk";
    put_u32(chunk, static_cast<uint32_t>(body.size()));
    return chunk + body;
}

struct MidiEvent { uint32_t tick; int order; unsigned char status, a, b; };   // order: offs (0) before ons (1)

// One tracker track's notes, or an empty list when it has none.
inline std::vector<MidiEvent> track_events(const Project& p, int trackIndex, const SongBounds& bounds,
                                           int swing, int& stepsWithFx, int& noteCount) {
    std::vector<MidiEvent> events;
    const Track& track = p.tracks[static_cast<size_t>(trackIndex)];
    const unsigned char channel = static_cast<unsigned char>(trackIndex & 15);
    const auto chain_at = [&](int row) -> const Chain* {
        if (row < 0 || row >= static_cast<int>(track.chainRefs.size())) return nullptr;
        const int id = track.chainRefs[static_cast<size_t>(row)];
        if (id < 0 || id >= static_cast<int>(p.chains.size())) return nullptr;
        const Chain& c = p.chains[static_cast<size_t>(id)];
        return chain_phrase_ref(c, 0) >= 0 ? &c : nullptr;   // a chain with no first phrase does not play
    };
    int row = bounds.startRow;
    while (row <= bounds.endRow && !chain_at(row)) ++row;   // the track's own first row
    uint32_t tick = 0;
    int sounding = -1;
    const auto close_note = [&](uint32_t at) {
        if (sounding >= 0) events.push_back({at, 0, static_cast<unsigned char>(0x80 | channel), static_cast<unsigned char>(sounding), 0});
        sounding = -1;
    };
    for (; row <= bounds.endRow; ++row) {
        const Chain* chain = chain_at(row);
        if (!chain) break;   // the block ends at the first row that does not play
        for (int cr = 0; cr < CHAIN_ROWS; ++cr) {
            const int ref = chain_phrase_ref(*chain, cr);
            if (ref < 0 || ref >= static_cast<int>(p.phrases.size())) continue;
            const Phrase& phrase = p.phrases[static_cast<size_t>(ref)];
            const int transpose = chain_transpose_semitones(*chain, cr) + project_transpose_semitones(p);
            for (int i = 0; i < PHRASE_ROWS; ++i) {
                const PhraseStep empty;
                const PhraseStep& step = i < static_cast<int>(phrase.steps.size()) ? phrase.steps[static_cast<size_t>(i)] : empty;
                if (step.fx1Type || step.fx2Type || step.fx3Type) ++stepsWithFx;
                // A row its NTH or PRB holds back on the first pass is not in the sketch (the same draw as in playback).
                if (!step_plays_on_pass(step, 1, prb_salt(trackIndex, ref, i))) { tick += static_cast<uint32_t>(swing_step_duration(kMidiStepTicks, swing, (i & 1) != 0)); continue; }
                if (note_is_off(step.note)) {
                    close_note(tick);
                } else if (note_is_pitch(step.note)) {
                    close_note(tick);
                    const int midi = note_to_midi(step.note) + transpose;
                    if (midi >= 0 && midi <= 127) {
                        const int velocity = std::clamp(step.volume, 1, 127);
                        events.push_back({tick, 1, static_cast<unsigned char>(0x90 | channel), static_cast<unsigned char>(midi),
                                          static_cast<unsigned char>(velocity)});
                        sounding = midi;
                        ++noteCount;
                    }
                }
                tick += static_cast<uint32_t>(swing_step_duration(kMidiStepTicks, swing, /*short_step=*/(i & 1) != 0));
            }
        }
    }
    close_note(tick);
    return events;
}

}  // namespace detail

// The file's bytes, or "" when the song has no notes to export.
inline std::string export_midi_file(const Project& p, MidiExportReport* report = nullptr) {
    MidiExportReport local;
    MidiExportReport& r = report ? *report : local;
    r = MidiExportReport{};
    const SongBounds bounds = find_song_bounds(p);
    if (bounds.empty()) return std::string();

    const int swing = std::clamp(p.swing, 50, 75);
    std::vector<std::string> trackChunks;
    for (size_t t = 0; t < p.tracks.size() && t < 16; ++t) {
        if (!track_audible(p, p.tracks[t])) continue;
        int notes = 0;
        std::vector<detail::MidiEvent> events = detail::track_events(p, static_cast<int>(t), bounds, swing, r.stepsWithEffects, notes);
        if (notes == 0) continue;
        std::stable_sort(events.begin(), events.end(), [](const detail::MidiEvent& a, const detail::MidiEvent& b) {
            return a.tick != b.tick ? a.tick < b.tick : a.order < b.order;
        });
        std::string body;
        detail::put_meta_text(body, 0, 0x03, "TRACK " + std::to_string(t + 1));
        uint32_t last = 0;
        for (const detail::MidiEvent& e : events) {
            detail::put_vlq(body, e.tick - last);
            last = e.tick;
            body.push_back(char(e.status));
            body.push_back(char(e.a));
            body.push_back(char(e.b));
        }
        detail::put_vlq(body, 0);
        body += std::string("\xFF\x2F\x00", 3);
        trackChunks.push_back(detail::mtrk(body));
        ++r.tracks;
        r.notes += notes;
    }
    if (r.notes == 0) return std::string();

    // Track 0: the tempo (what the song plays at) and the meter.
    std::string conductor;
    detail::put_meta_text(conductor, 0, 0x03, p.name.empty() ? std::string("TRACKWERK") : p.name);
    detail::put_vlq(conductor, 0);
    const uint32_t micros = static_cast<uint32_t>(60000000 / std::max(1, bounded_tempo(p.tempo)));
    conductor += std::string("\xFF\x51\x03", 3);
    conductor.push_back(char((micros >> 16) & 255));
    conductor.push_back(char((micros >> 8) & 255));
    conductor.push_back(char(micros & 255));
    detail::put_vlq(conductor, 0);
    conductor += std::string("\xFF\x58\x04\x04\x02\x18\x08", 7);   // 4/4
    detail::put_vlq(conductor, 0);
    conductor += std::string("\xFF\x2F\x00", 3);

    std::string file = "MThd";
    detail::put_u32(file, 6);
    detail::put_u16(file, 1);
    detail::put_u16(file, static_cast<int>(trackChunks.size()) + 1);
    detail::put_u16(file, detail::kMidiPpq);
    file += detail::mtrk(conductor);
    for (const std::string& chunk : trackChunks) file += chunk;
    return file;
}

}  // namespace songcore

#endif
