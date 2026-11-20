// ptsample-editor-save-exit-check: Codex's review-16 probe (reproduktionen/patch41-editor-remaining.cpp),
// adapted for patch 0042 (Claude Code): saving ends the audition; marker rescale cannot fail; a refused pitch waveform still saves.
// Host probes for normal OVERWRITE and two allocation failures adjacent to Patch 0041.
#include "audio-engine.h"
#include "songcore/host.h"
#include "songcore/project_io.h"
#include "songcore/wav_writer.h"
#include "ui/input_dispatcher.h"
#include "ui/std_filesystem.h"

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <new>
#include <string>
#include <vector>
#include <unistd.h>
#include <cstdio>

namespace {
size_t failBytes = 0;
int skip = 0;
}

void* operator new(size_t bytes) {
    if (failBytes && bytes == failBytes) {
        if (skip == 0) {
            failBytes = 0;
            throw std::bad_alloc{};
        }
        --skip;
    }
    if (void* memory = std::malloc(bytes ? bytes : 1)) return memory;
    throw std::bad_alloc{};
}

void operator delete(void* memory) noexcept { std::free(memory); }
void* operator new[](size_t bytes) { return ::operator new(bytes); }
void operator delete[](void* memory) noexcept { ::operator delete(memory); }

namespace {
constexpr int frames = 192000;
constexpr int fileRate = 48000;
constexpr int instrument = 1;

struct Sample {
    std::unique_ptr<AudioEngine> engine = std::make_unique<AudioEngine>();
    songcore::SongcoreHost host{engine.get(), fileRate};
    pt::ui::AppState state;
    pt::ui::StdFileSystem fs;
    pt::ui::InputDispatcher dispatch{state, host, fs};
    std::filesystem::path wav;
    int slot = -1;

    Sample(const std::filesystem::path& path, bool stereo, int sourceRate = fileRate, int frameCount = frames, int fileMarkers = 0) : fs(path.string()), wav(path / "input.wav") {
        engine->setDeviceSampleRate(fileRate);
        std::filesystem::create_directories(path);
        const int channels = stereo ? 2 : 1;
        std::vector<float> left(frameCount), right(stereo ? frameCount : 0);
        for (int i = 0; i < frameCount; ++i) {
            left[i] = .51f * std::sin(.031f * i);
            if (stereo) right[i] = .34f * std::sin(.037f * i);
        }
        std::vector<int> cues;
        for (int n = 1; n <= fileMarkers; ++n) cues.push_back(n * 8);
        assert(songcore::write_wav(wav.string(), left, right, sourceRate, cues, channels));
        auto project = songcore::make_default_project();
        project.instruments[instrument].sampleFilePath = wav.string();
        assert(host.push_project(songcore::serialize_project(project)));
        assert(host.load_media("").failed == 0);
        slot = host.project().instruments[instrument].sampleId;
        assert(host.sample_length(slot) == frameCount);
        state.project = &host.edit_project();
        state.currentInstrument = instrument;
        state.currentScreen = pt::ui::ScreenType::INSTRUMENT;
        dispatch.open_imported_sample_editor();
        assert(state.currentScreen == pt::ui::ScreenType::SAMPLE_EDITOR);
    }

    void tap(int row, int column) {
        state.sampleEditor.cursorRow = row;
        state.sampleEditor.cursorCol = column;
        dispatch.on_button_a();
    }
};

void configure_sync(Sample& s, bool stretch, int duration, int pitch) {
    using pt::ui::SampleEditorModule;
    auto& editor = s.state.sampleEditor;
    editor.fxType = SampleEditorModule::FX_SYNC;
    editor.syncType = stretch ? SampleEditorModule::SYNC_TSTRETCH : SampleEditorModule::SYNC_RPITCH;
    editor.pitchSemitones = pitch;
    editor.stretchBpm = 120;
    editor.stretchGrainMs = 40;
    editor.durationIndex = duration;
    s.host.edit_project().tempo = 120;
}

double energy(Sample& s) {
    double sum = 0;
    for (int n = 0; n < 8; ++n) {
        float out[512]{};
        s.engine->processLiveBlock(out, 256, 2, 48000.f);
        for (float sample : out) sum += sample * sample;
    }
    return sum;
}

void save_preview(const std::filesystem::path& root, bool listenFirst, bool overwrite) {
    Sample s(root, true);
    s.dispatch.on_start();
    if (listenFirst) assert(energy(s) > 0.01);
    if (!overwrite) s.state.sampleEditor.sampleName = "PREVIEW EXIT";
    s.tap(19, overwrite ? 2 : 1); // successful stereo SAVE or OVERWRITE, leaves the editor
    assert(s.state.currentScreen != pt::ui::ScreenType::SAMPLE_EDITOR);
    // A ringing voice ends with the engine's short de-click fade inside the first blocks; after
    // that, and for a queued note from the start, there is nothing.
    const double fade = energy(s);
    const double sound = energy(s);
    std::cout << (overwrite ? "OVERWRITE" : "SAVE") << " after " << (listenFirst ? "running" : "queued")
              << " STEREO START: fade-out energy " << fade << ", then " << sound << '\n';
    assert(sound < 0.00001 && (listenFirst || fade < 0.00001));
}

void rescale_refusal(const std::filesystem::path& root) {
    Sample s(root, true, 48000, frames, 256);
    auto& se = s.state.sampleEditor;
    configure_sync(s, true, 2, 0);
    const auto generation = s.host.sample_generation(s.slot);
    assert(se.fileMarkers.size() == 256 && se.fileMarkers.front() == 8);
    failBytes = 256 * sizeof(int);
    skip = 1; // let the Undo snapshot succeed, then refuse rescale_positions' file list
    bool escaped = false;
    try { s.tap(16, 2); } catch (const std::bad_alloc&) { escaped = true; }
    std::cout << "SYNC marker rescale refusal: exception " << escaped
              << ", engine " << s.host.sample_length(s.slot) << ", editor " << se.totalFrames
              << ", first marker " << se.fileMarkers.front() << ", modified " << se.isModified
              << ", generation +" << s.host.sample_generation(s.slot) - generation << '\n';
    failBytes = 0; skip = 0;   // rescale_positions no longer allocates: nothing to refuse there
    assert(!escaped && s.host.sample_length(s.slot) == 96000);
    assert(se.totalFrames == 96000 && se.fileMarkers.size() == 256 && se.fileMarkers.front() == 4 && se.isModified);
    assert(s.host.sample_generation(s.slot) == generation + 1);
    s.tap(14, 5);   // Undo: the old audio with its markers
    assert(s.host.sample_length(s.slot) == frames && se.totalFrames == frames && se.fileMarkers.front() == 8);
}

void pitch_waveform_refusal(const std::filesystem::path& root) {
    Sample s(root, true, 48000, frames, 256);
    auto& se = s.state.sampleEditor;
    se.pitchSemitones = 12;
    const auto generation = s.host.sample_generation(s.slot);
    failBytes = pt::ui::SampleEditorModule::waveform_w() * 2 * sizeof(float);
    bool escaped = false;
    try { s.tap(19, 2); } catch (const std::bad_alloc&) { escaped = true; }
    std::cout << "Pitch OVERWRITE waveform refusal: exception " << escaped
              << ", engine " << s.host.sample_length(s.slot) << ", editor " << se.totalFrames
              << ", pending pitch " << se.pitchSemitones << ", modified " << se.isModified
              << ", generation +" << s.host.sample_generation(s.slot) - generation << '\n';
    // Only the picture was refused: the save goes on with the pitched audio and its markers.
    assert(!escaped && failBytes == 0 && s.host.sample_length(s.slot) == 96000);
    assert(se.totalFrames == 96000 && se.pitchSemitones == 0 && !se.isModified);
    assert(s.state.currentScreen != pt::ui::ScreenType::SAMPLE_EDITOR);
    const auto cues = songcore::read_cue_points(s.wav.string());
    assert(cues.size() == 256 && cues.front() == 4);
}
} // namespace

int main(int argc, char** argv) {
    assert(argc == 2);
    // A directory of its own, new for this run (mkdtemp), and only that is removed at the end.
    std::filesystem::create_directories(argv[1]);
    std::string tmpl = (std::filesystem::path(argv[1]) / "ptsample-editor-save-exit.XXXXXX").string();
    assert(mkdtemp(tmpl.data()) != nullptr);
    const std::filesystem::path root = tmpl;
    save_preview(root / "overwrite-queued", false, true);
    save_preview(root / "overwrite-running", true, true);
    save_preview(root / "save-queued", false, false);
    rescale_refusal(root / "rescale-refusal");
    pitch_waveform_refusal(root / "pitch-waveform");
    std::filesystem::remove_all(root);
    std::puts("PASS: SAVE and OVERWRITE end a queued or ringing audition; SYNC rescales markers without allocating; a refused pitch waveform still saves the pitched audio with its cues");
}
