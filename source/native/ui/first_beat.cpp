#include "ui/first_beat.h"
#include "ui/modules/help_overlay.h"

#include "songcore/model.h"
#include "ui/app_state.h"
#include "ui/canvas.h"
#include "ui/helpers.h"
#include "ui/modules/oscilloscope.h"
#include "ui/sample_granular_recording.h"
#include "ui/scene_layout.h"

#include <algorithm>
#include <string>

namespace pt::ui {

namespace {

// The phrase being edited: the one the chain led into.
const songcore::Phrase* tutorial_phrase(const AppState& s) {
    if (!s.project || s.currentPhrase < 0 || s.currentPhrase >= static_cast<int>(s.project->phrases.size()))
        return nullptr;
    return &s.project->phrases[static_cast<size_t>(s.currentPhrase)];
}

int note_value(const songcore::PhraseStep& step) {
    return songcore::step_has_note(step) ? step.note.octave * 12 + step.note.pitch : -1;
}

// Some chain in the song; with `withPhrase`, one that holds a phrase.
bool chain_in_song(const songcore::Project& p, bool withPhrase) {
    for (const auto& track : p.tracks)
        for (const int chain : track.chainRefs) {
            if (chain < 0 || chain >= static_cast<int>(p.chains.size())) continue;
            if (!withPhrase) return true;
            const auto& refs = p.chains[static_cast<size_t>(chain)].phraseRefs;
            if (std::any_of(refs.begin(), refs.end(), [](int ph) { return ph >= 0; })) return true;
        }
    return false;
}

}  // namespace

FirstBeatText first_beat_text(FirstBeatStep step) {
    switch (step) {
        case FirstBeatStep::SONG_CHAIN:
            return {"1/10  A chain into the song", "The SONG plays chains. Put one in the first row.",
                    "A on the first row of track 1"};
        case FirstBeatStep::CHAIN_PHRASE:
            return {"2/10  A phrase into the chain", "A chain plays phrases. Open it and put one in.",
                    "Hold R, press Right. Then A on the first row"};
        case FirstBeatStep::GO_PHRASE:
            return {"3/10  Open the phrase", "A phrase holds the notes: 16 steps, top to bottom.",
                    "Hold R, press Right"};
        case FirstBeatStep::SOUND:
            return {"4/10  Give instrument 00 a sound", "INSTRUMENT: down to STUDIO >, A. Down to APPLY, A.",
                    "Hold R, press Right. In Studio B goes back"};
        case FirstBeatStep::NOTES:
            return {"5/10  Write four notes", "Back on PHRASE, put notes in the NOTE column.",
                    "Hold R, Left to PHRASE  A: note  Down: next step"};
        case FirstBeatStep::PLAY:
            return {"6/10  Listen", "Play the phrase. START again stops it.", "START"};
        case FirstBeatStep::CHANGE:
            return {"7/10  Change a note", "Hold A on a note and move it up or down.",
                    "Hold A, press Left / Right or Up / Down"};
        case FirstBeatStep::REMOVE:
            return {"8/10  Delete a note", "Take one note out again.", "On the note hold A, press B"};
        case FirstBeatStep::SAVE:
            return {"9/10  Save your beat", "It is saved as its own project; nothing else changes.",
                    "Hold R: Left to SONG, then Up. Row PROJECT, A on SAVE"};
        case FirstBeatStep::RELOAD:
            return {"10/10  Load it again", "PROJECT: LOAD, then pick your FIRSTBEAT file.",
                    "A on LOAD, then A on the file"};
        case FirstBeatStep::DONE:
            return {"Done: your first beat", "Keep going: more notes, more phrases, other synths.",
                    "SELECT, then START closes this card"};
    }
    return {"", "", ""};
}

const char* first_beat_blocked(const AppState& s) {
    // An unsaved edit in the open Sample Editor lives only in RAM: a new project would drop it without
    // asking. Refused as the take paths do ("Save current sample edits first"). A closed editor's
    // flag is no guard: leaving it with unsaved work already asked (discard or stay).
    if (s.currentScreen == ScreenType::SAMPLE_EDITOR && s.sampleEditor.isModified)
        return "FIRST BEAT: SAVE THE SAMPLE EDIT FIRST";
    if (s.capture.active()) return "FIRST BEAT: STOP THE RECORDING FIRST";
    if (s.resampleView.active || s.resampleView.saving) return "FIRST BEAT: FINISH THE RESAMPLE FIRST";
    if (granular_recording_busy(s.sampleGranular.recording)) return "FIRST BEAT: FINISH THE GRANULAR TAKE FIRST";
    const auto& src = s.sourceSampler;
    if (src.recording || src.rendering || src.busy) return "FIRST BEAT: SOURCE SAMPLER IS BUSY";
    return nullptr;
}

bool first_beat_invite(const AppState& s) {
    return s.project && !s.firstBeat.active && !s.settings.firstBeatDone && !chain_in_song(*s.project, false);
}

void first_beat_tell_blocked(AppState& s, const char* why) {
    s.statusMessage = why;
    s.statusSuccess = false;
    if (s.currentScreen == ScreenType::SAMPLE_EDITOR && s.sampleEditor.isModified)
        s.sampleEditor.toolMessage = FIRST_BEAT_SAMPLE_HINT;
}

int first_beat_note_count(const AppState& s) {
    const auto* phrase = tutorial_phrase(s);
    if (!phrase) return 0;
    return static_cast<int>(std::count_if(phrase->steps.begin(), phrase->steps.end(),
                                          [](const songcore::PhraseStep& st) { return songcore::step_has_note(st); }));
}

void first_beat_snapshot(AppState& s) {
    auto& fb = s.firstBeat;
    fb.notesAtStep.fill(-1);
    fb.notesCountAtStep = 0;
    fb.phraseAtStep = s.currentPhrase;
    fb.loadsAtStep = s.projectLoads;
    if (!s.project) return;
    if (const auto* phrase = tutorial_phrase(s)) {
        for (size_t i = 0; i < phrase->steps.size() && i < fb.notesAtStep.size(); ++i)
            fb.notesAtStep[i] = note_value(phrase->steps[i]);
        fb.notesCountAtStep = first_beat_note_count(s);
    }
}

bool first_beat_step_done(const AppState& s, bool playing) {
    if (!s.project) return false;
    const auto& p = *s.project;
    const auto& fb = s.firstBeat;
    switch (fb.step) {
        case FirstBeatStep::SONG_CHAIN:
            return chain_in_song(p, false);
        case FirstBeatStep::CHAIN_PHRASE:
            return chain_in_song(p, true);
        case FirstBeatStep::GO_PHRASE:
            return !s.studioOpen && s.currentScreen == ScreenType::PHRASE;
        case FirstBeatStep::SOUND:
            return !p.instruments.empty() && !songcore::instrument_is_free(p.instruments[0]);
        case FirstBeatStep::NOTES:
            return s.currentPhrase == fb.phraseAtStep && first_beat_note_count(s) >= 4;
        case FirstBeatStep::PLAY:
            return playing;
        case FirstBeatStep::CHANGE: {
            const auto* phrase = tutorial_phrase(s);
            // Only an edit in the same phrase: moving to another phrase is no changed note.
            if (!phrase || s.currentPhrase != fb.phraseAtStep) return false;
            for (size_t i = 0; i < phrase->steps.size() && i < fb.notesAtStep.size(); ++i) {
                const int now = note_value(phrase->steps[i]);
                if (fb.notesAtStep[i] >= 0 && now >= 0 && now != fb.notesAtStep[i]) return true;
            }
            return false;
        }
        case FirstBeatStep::REMOVE:
            return s.currentPhrase == fb.phraseAtStep && first_beat_note_count(s) < fb.notesCountAtStep;
        case FirstBeatStep::SAVE:
            // Saved, clean, and under the tutorial's own name — never another project.
            return !s.project_dirty() && !s.projectPath.empty() && p.name == fb.projectName;
        case FirstBeatStep::RELOAD:
            return s.projectLoads > fb.loadsAtStep && p.name == fb.projectName;
        case FirstBeatStep::DONE:
            return false;
    }
    return false;
}

bool first_beat_poll(AppState& s, bool playing) {
    auto& fb = s.firstBeat;
    if (!fb.active) return false;
    if (fb.reviewing) {
        if (s.projectVersion == fb.reviewVersion && static_cast<int>(s.currentScreen) == fb.reviewScreen &&
            playing == fb.reviewPlaying)
            return false;
        fb.reviewing = false;
    }
    bool changed = false;
    // A step done already (a chain the player put in early) is ticked off in the same frame.
    while (fb.step != FirstBeatStep::DONE && first_beat_step_done(s, playing)) {
        fb.step = static_cast<FirstBeatStep>(static_cast<int>(fb.step) + 1);
        first_beat_snapshot(s);
        changed = true;
    }
    if (changed && fb.step == FirstBeatStep::DONE) s.settings.firstBeatDone = true;
    return changed;
}

void first_beat_step_back(AppState& s) {
    auto& fb = s.firstBeat;
    if (!fb.active || fb.step == FirstBeatStep::SONG_CHAIN) return;
    fb.step = static_cast<FirstBeatStep>(static_cast<int>(fb.step) - 1);
    first_beat_snapshot(s);
    fb.reviewing = true;
    fb.reviewVersion = s.projectVersion;
    fb.reviewScreen = static_cast<int>(s.currentScreen);
    fb.reviewPlaying = s.isPlaying;
}

void draw_first_beat(Canvas& c, const AppState& s) {
    if (!s.firstBeat.active) return;
    const Theme& t = s.theme;
    // Over the scope strip on the tracker screens, which keeps the editor in view; on the Studio over
    // its footer hints.
    const bool settings = s.currentScreen == ScreenType::SETTINGS;
    const auto scene = settings ? SceneLayout{} : scene_layout(t);
    const int w = OscilloscopeModule::WIDTH, h = OscilloscopeModule::HEIGHT;
    const int x = SIDE_SPACER;
    const int y = s.studioOpen ? DESIGN_H - h - 4
                : scene.scopeBottom ? DESIGN_H - SCREEN_SPACER - h : SCREEN_SPACER;
    const FirstBeatText text = first_beat_text(s.firstBeat.step);
    c.reset_clip();
    c.panel_rect(x, y, w, h, t.background, t.rowCursor, 5);
    c.fill_rect(x + 1, y + 1, 4, h - 2, t.rowCursor);
    // Progress: one pip per step, the done ones lit.
    const int done = static_cast<int>(s.firstBeat.step);
    for (int i = 0; i < FIRST_BEAT_STEPS; ++i)
        c.fill_rect(x + w - 14 - (FIRST_BEAT_STEPS - i) * 12, y + 10, 9, 5,
                    i < done ? t.rowCursor : with_alpha(t.textParam, .25f));
    c.draw_label("FIRST BEAT", x + 14, y + 6, t.rowCursor, 12);
    c.draw_label(Canvas::clip_label(text.title, w - 260, 12), x + 104, y + 6, t.textTitle, 12);
    c.draw_label(Canvas::clip_label(text.todo, w - 28, 15), x + 14, y + 24, t.textValue, 15);
    std::string keys = text.keys;
    if (s.caps.touchLayouts) keys += "   (on-screen buttons)";
    c.draw_label(Canvas::clip_label(keys, w - 28, 12), x + 14, y + 48, t.textParam, 12);
}

void draw_first_beat_help_strip(Canvas& c, const AppState& s) {
    const Theme& t = s.theme;
    const int h = help_overlay_geometry::FOOT_H, y = DESIGN_H - h;   // the full help stays above it
    c.reset_clip();
    c.fill_rect(0, y, DESIGN_W, h, with_alpha(t.background, .94f));
    c.fill_rect(0, y, DESIGN_W, 1, t.rowCursor);
    const char* text = !s.firstBeat.active          ? "FIRST BEAT   START: learn the basics step by step"
                     : s.firstBeat.step == FirstBeatStep::DONE ? "FIRST BEAT   START: close the card"
                                                               : "FIRST BEAT   START: end    L: step back";
    c.draw_label(text, SIDE_SPACER + 4, y + 6, t.rowCursor, 12);
}

}  // namespace pt::ui
