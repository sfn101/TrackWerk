// SPDX-License-Identifier: GPL-3.0-or-later
// ptstep-probability-check: the PRB command. A phrase row with "PRB xx" plays with the chance xx/256
// on each pass; the draw is a fixed function of track, phrase, row and pass, so every play-through, a
// render and the MIDI export agree, and different rows and passes draw differently.
#include "songcore/effects.h"
#include "songcore/midi_export.h"
#include "songcore/project_io.h"
#include "songcore/scheduler.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <set>
#include <vector>

using namespace songcore;

namespace {

struct Capture : IMidiConsumer {
    std::vector<std::pair<int64_t, int>> notes;   // frame, midi note
    void consume(const Event& e) override { if (e.type == EV_NOTE_ON) notes.push_back({e.frame, e.noteOn.note}); }
    void on_play(const std::string&, const std::string&, int64_t, int, int) override {}
    void on_stop() override {}
};

// A song of one track: one chain that enters the same phrase `entries` times (passes 1..entries).
Project song(int prb, int entries, int nthSlot = 0) {
    Project p = make_default_project();
    normalize_and_migrate(p);
    p.tracks[0].chainRefs = {0};
    for (int i = 0; i < 16; ++i) p.chains[0].phraseRefs[size_t(i)] = i < entries ? 0 : -1;
    for (int i = 0; i < 16; ++i) {
        auto& step = p.phrases[0].steps[size_t(i)];
        step.note = Note{i % 12, 4};
        step.fx1Type = FX_PRB;
        step.fx1Value = prb;
        if (nthSlot) { step.fx2Type = FX_NTH; step.fx2Value = nthSlot; }
    }
    return p;
}
std::vector<std::pair<int64_t, int>> play(const Project& p) {
    Capture c;
    MidiRouter router(&c);
    Sequencer seq(router, p, 48000);
    seq.scheduleSongRowRange(0, 0, nullptr);
    return c.notes;
}

}  // namespace

int main() {
    // The command exists: named, listed once, MIDI commands still the tail of the list.
    assert(std::string(effect_name(FX_PRB)) == "PRB" && FX_PRB == 0x40);
    int listed = 0;
    for (int t : EFFECT_TYPES) listed += t == FX_PRB;
    assert(listed == 1 && EFFECT_TYPES[EFFECT_TYPE_COUNT - 1] == FX_CCD);

    // The draw: 00 and FF always, the chance in between within a point or two over many rows and passes.
    {
        int always = 0, rows = 0;
        for (uint32_t salt = 0; salt < 400; ++salt)
            for (int pass = 1; pass <= 25; ++pass) { always += prb_plays(0x00, salt, pass) && prb_plays(0xFF, salt, pass); ++rows; }
        assert(always == rows);
        for (int value : {1, 32, 64, 128, 192, 254}) {
            int played = 0;
            for (uint32_t salt = 0; salt < 400; ++salt)
                for (int pass = 1; pass <= 25; ++pass) played += prb_plays(value, prb_salt(0, 7, int(salt % 16)) + salt * 977u, pass);
            const double share = double(played) / rows, expected = value / 256.0;
            std::printf("PRB %02X: %.1f %% played (expected %.1f %%)\n", value, 100 * share, 100 * expected);
            assert(std::fabs(share - expected) < .02);
        }
        // The same draw, always; a different row or pass is a different draw.
        for (uint32_t salt = 0; salt < 50; ++salt) assert(prb_plays(128, salt, 3) == prb_plays(128, salt, 3));
        std::set<int> rowsDiffer, passesDiffer;
        for (int row = 0; row < 16; ++row) rowsDiffer.insert(prb_plays(128, prb_salt(0, 0, row), 1));
        for (int pass = 1; pass <= 16; ++pass) passesDiffer.insert(prb_plays(128, prb_salt(0, 0, 5), pass));
        assert(rowsDiffer.size() == 2 && passesDiffer.size() == 2);
    }

    // In the scheduler: all rows of 16 × 4 passes. FF and 00 play everything, 80 about half, 01 almost none.
    {
        assert(play(song(0xFF, 4)).size() == 64 && play(song(0x00, 4)).size() == 64);
        const auto half = play(song(0x80, 4));
        std::printf("PRB 80 over 64 rows: %zu notes\n", half.size());
        assert(half.size() >= 20 && half.size() <= 44);
        assert(play(song(0x01, 4)).size() <= 4);
        assert(play(song(0xFE, 4)).size() >= 60);
        assert(play(song(0x80, 4)) == half);   // a second play-through: identical notes at identical frames
        // A held-back row takes its time: the frames of the notes that did play are those of their rows.
        std::set<int64_t> frames;
        for (const auto& n : half) frames.insert(n.first);
        assert(frames.size() == half.size());
    }

    // With NTH on the same row both must agree: PRB FF + NTH 11 (every pass) is all rows; PRB FF + NTH 20
    // (pass 2 only) is 16 rows; PRB 00 + NTH 20 the same.
    {
        assert(play(song(0xFF, 4, 0x11)).size() == 64);
        assert(play(song(0xFF, 4, 0x20)).size() == 16);
        assert(play(song(0x00, 4, 0x20)).size() == 16);
        const auto both = play(song(0x80, 4, 0x20));   // only pass 2, and then only half of its rows
        assert(both.size() < 16 && both.size() > 2);
    }

    // The MIDI export draws the same way for the first pass: PRB rows held back are not in the sketch.
    {
        const Project p = song(0x80, 1);
        MidiExportReport rep;
        const std::string file = export_midi_file(p, &rep);
        const size_t played = play(p).size();
        std::printf("pass 1: %zu of 16 rows play; the MIDI sketch has %d notes\n", played, rep.notes);
        assert(played > 0 && size_t(rep.notes) == played);
    }
    std::printf("STEP_PROBABILITY_PASS\n");
}
