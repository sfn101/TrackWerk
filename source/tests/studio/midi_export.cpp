// SPDX-License-Identifier: GPL-3.0-or-later
// ptmidi-export-check: the song as a Standard MIDI File (songcore/midi_export.h), read back with a
// small independent parser: header, tempo, channels, note times with swing, transpose, velocity, OFF,
// chains and song rows in sequence, mute and solo, empty cases and what the report counts.
#include "songcore/midi_export.h"
#include "songcore/project_io.h"

#include <cassert>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

using namespace songcore;

namespace {

struct Ev { uint32_t tick; int channel; int kind; int a, b; };   // kind: 0x90 on, 0x80 off
struct Parsed {
    int format = -1, tracks = -1, division = -1;
    uint32_t micros = 0;
    std::vector<std::vector<Ev>> perTrack;   // index 0 is the conductor
    std::vector<std::string> names;
    bool timeSig44 = false;
    bool ok = false;
};

uint32_t u32(const std::string& s, size_t at) { return (uint32_t(uint8_t(s[at])) << 24) | (uint32_t(uint8_t(s[at + 1])) << 16) | (uint32_t(uint8_t(s[at + 2])) << 8) | uint8_t(s[at + 3]); }
int u16(const std::string& s, size_t at) { return (uint8_t(s[at]) << 8) | uint8_t(s[at + 1]); }

Parsed parse(const std::string& f) {
    Parsed r;
    if (f.size() < 14 || f.compare(0, 4, "MThd") != 0 || u32(f, 4) != 6) return r;
    r.format = u16(f, 8); r.tracks = u16(f, 10); r.division = u16(f, 12);
    size_t at = 14;
    for (int t = 0; t < r.tracks; ++t) {
        if (at + 8 > f.size() || f.compare(at, 4, "MTrk") != 0) return r;
        const size_t len = u32(f, at + 4), end = at + 8 + len;
        if (end > f.size()) return r;
        size_t p = at + 8;
        uint32_t tick = 0;
        std::vector<Ev> evs;
        std::string name;
        bool ended = false;
        while (p < end) {
            uint32_t delta = 0; int n = 0;
            while (true) { const uint8_t b = uint8_t(f[p++]); delta = (delta << 7) | (b & 0x7F); if (!(b & 0x80) || ++n > 3) break; }
            tick += delta;
            const uint8_t status = uint8_t(f[p++]);
            if (status == 0xFF) {
                const uint8_t type = uint8_t(f[p++]);
                uint32_t l = 0; int m = 0;
                while (true) { const uint8_t b = uint8_t(f[p++]); l = (l << 7) | (b & 0x7F); if (!(b & 0x80) || ++m > 3) break; }
                if (type == 0x03) name.assign(f, p, l);
                if (type == 0x51 && l == 3) r.micros = (uint32_t(uint8_t(f[p])) << 16) | (uint32_t(uint8_t(f[p + 1])) << 8) | uint8_t(f[p + 2]);
                if (type == 0x58 && l == 4 && uint8_t(f[p]) == 4 && uint8_t(f[p + 1]) == 2) r.timeSig44 = true;
                if (type == 0x2F) ended = true;
                p += l;
            } else if ((status & 0xF0) == 0x90 || (status & 0xF0) == 0x80) {
                evs.push_back({tick, status & 15, status & 0xF0, uint8_t(f[p]), uint8_t(f[p + 1])});
                p += 2;
            } else {
                return r;   // nothing else is written
            }
        }
        if (!ended || p != end) return r;
        r.perTrack.push_back(evs);
        r.names.push_back(name);
        at = end;
    }
    r.ok = at == f.size();
    return r;
}

Project base() {
    Project p = make_default_project();
    normalize_and_migrate(p);
    p.tempo = 120;
    p.swing = 50;
    p.transpose = 0;
    return p;
}
void phrase(Project& p, int id, std::vector<std::pair<int, Note>> notes) {
    for (auto& [step, note] : notes) { p.phrases[size_t(id)].steps[size_t(step)].note = note; p.phrases[size_t(id)].steps[size_t(step)].volume = 0x7F; }
}
void play(Project& p, int track, int songRow, int chain, std::vector<int> phrases) {
    auto& refs = p.tracks[size_t(track)].chainRefs;
    while (int(refs.size()) <= songRow) refs.push_back(-1);
    refs[size_t(songRow)] = chain;
    for (size_t i = 0; i < phrases.size(); ++i) p.chains[size_t(chain)].phraseRefs[i] = phrases[i];
}

}  // namespace

int main() {
    // 1 · One track, one phrase: header, tempo, meter, names, the notes where the steps are.
    {
        Project p = base(); p.name = "DUBSONG";
        phrase(p, 0, {{0, Note{0, 4}}, {4, Note{7, 4}}, {8, Note::OFF()}, {12, Note{2, 5}}});
        play(p, 0, 0, 0, {0});
        MidiExportReport rep;
        const std::string f = export_midi_file(p, &rep);
        const Parsed m = parse(f);
        assert(m.ok && m.format == 1 && m.tracks == 2 && m.division == 480);
        assert(m.micros == 500000 && m.timeSig44 && m.names[0] == "DUBSONG" && m.names[1] == "TRACK 1");
        const auto& e = m.perTrack[1];
        // C4 at step 0, G4 at step 4 (ends C4), OFF at step 8 (ends G4), D5 at step 12; it ends at step 16.
        assert(e.size() == 6);
        auto at = [&](size_t i, uint32_t tick, int kind, int note) { return e[i].tick == tick && e[i].kind == kind && e[i].a == note && e[i].channel == 0; };
        assert(at(0, 0, 0x90, 60) && e[0].b == 127);
        assert(at(1, 480, 0x80, 60) && at(2, 480, 0x90, 67));   // the off comes first
        assert(at(3, 960, 0x80, 67));
        assert(at(4, 1440, 0x90, 74) && at(5, 1920, 0x80, 74));
        assert(rep.tracks == 1 && rep.notes == 3 && rep.stepsWithEffects == 0);
    }

    // 2 · Swing: odd steps are late. At 75 % the pair is 3:1 (long 180 ticks, short 60).
    {
        Project p = base(); p.swing = 75;
        phrase(p, 0, {{0, Note{0, 4}}, {1, Note{2, 4}}, {2, Note{4, 4}}, {3, Note{5, 4}}});
        play(p, 0, 0, 0, {0});
        const Parsed m = parse(export_midi_file(p));
        std::vector<uint32_t> ons;
        for (const Ev& e : m.perTrack[1]) if (e.kind == 0x90) ons.push_back(e.tick);
        assert((ons == std::vector<uint32_t>{0, 180, 240, 420}));
    }

    // 3 · Transpose (project + chain row), velocity from the step, notes out of range skipped.
    {
        Project p = base(); p.transpose = 2;
        phrase(p, 0, {{0, Note{0, 4}}, {1, Note{0, 4}}, {2, note_from_midi(127)}});
        p.phrases[0].steps[1].volume = 0x20;
        p.phrases[0].steps[0].volume = 0xFF;   // above 127: clamped
        play(p, 0, 0, 0, {0});
        p.chains[0].transposeValues[0] = 0xFF;   // -1
        const Parsed m = parse(export_midi_file(p));
        std::vector<int> pitches, velocities;
        for (const Ev& e : m.perTrack[1]) if (e.kind == 0x90) { pitches.push_back(e.a); velocities.push_back(e.b); }
        assert((pitches == std::vector<int>{61, 61}));   // 60 + 2 - 1; 127 + 1 does not exist
        assert((velocities == std::vector<int>{127, 32}));
    }

    // 4 · A chain of two phrases, then a second song row: 16 + 16 + 16 steps in a row; empty chain
    // rows are skipped; the track's notes on one channel.
    {
        Project p = base();
        phrase(p, 0, {{0, Note{0, 4}}});
        phrase(p, 1, {{0, Note{2, 4}}});
        phrase(p, 2, {{0, Note{4, 4}}});
        play(p, 0, 0, 0, {0, -1, 1});
        play(p, 0, 1, 1, {2});
        const Parsed m = parse(export_midi_file(p));
        std::vector<uint32_t> ons;
        for (const Ev& e : m.perTrack[1]) if (e.kind == 0x90) ons.push_back(e.tick);
        assert((ons == std::vector<uint32_t>{0, 1920, 3840}));
        for (const Ev& e : m.perTrack[1]) assert(e.channel == 0);
    }

    // 5 · The block ends at the first row that does not play, and a track starts at its own first row.
    {
        Project p = base();
        phrase(p, 0, {{0, Note{0, 4}}});
        phrase(p, 1, {{0, Note{2, 4}}});
        play(p, 0, 0, 0, {0});
        play(p, 0, 2, 1, {1});                         // after a gap: not part of the first block
        play(p, 1, 1, 1, {1});                         // track 2 starts at song row 1 (its own first row)
        const Parsed m = parse(export_midi_file(p));
        assert(m.tracks == 3);
        int ons0 = 0, ons1 = 0;
        for (const Ev& e : m.perTrack[1]) ons0 += e.kind == 0x90;
        for (const Ev& e : m.perTrack[2]) { ons1 += e.kind == 0x90; assert(e.channel == 1 && e.tick == 0 || e.kind == 0x80); }
        assert(ons0 == 1 && ons1 == 1);
    }

    // 6 · Mute, solo: what the mix would not play is not exported.
    {
        Project p = base();
        phrase(p, 0, {{0, Note{0, 4}}});
        for (int t = 0; t < 3; ++t) play(p, t, 0, 0, {0});
        p.tracks[1].mute = true;
        assert(parse(export_midi_file(p)).tracks == 3);   // conductor + tracks 1 and 3
        p.tracks[2].solo = true;
        const Parsed m = parse(export_midi_file(p));
        assert(m.tracks == 2 && m.names[1] == "TRACK 3");
    }

    // 7 · Empty song, a song without notes, and corrupt references: nothing, and no crash.
    {
        Project p = base();
        assert(export_midi_file(p).empty());
        play(p, 0, 0, 0, {0});                           // a phrase of rests
        assert(export_midi_file(p).empty());
        p.tracks[0].chainRefs[0] = 9999;
        p.chains[0].phraseRefs[0] = 9999;
        assert(export_midi_file(p).empty());
    }

    // 8 · Effects are counted, not written; long deltas use two VLQ bytes and parse back.
    {
        Project p = base();
        phrase(p, 0, {{0, Note{0, 4}}});
        p.phrases[0].steps[3].fx1Type = 5; p.phrases[0].steps[3].fx1Value = 1;
        p.phrases[0].steps[9].fx2Type = 2;
        play(p, 0, 0, 0, {0});
        MidiExportReport rep;
        const std::string f = export_midi_file(p, &rep);
        assert(rep.stepsWithEffects == 2 && parse(f).ok);
        for (int row = 1; row < 40; ++row) play(p, 0, row, 0, {0});   // 40 phrases: a long song
        const Parsed m = parse(export_midi_file(p));
        assert(m.ok && m.perTrack[1].back().tick == 40u * 1920u);
    }
    std::printf("MIDI_EXPORT_PASS\n");
}
