// ptsample-editor-marker-budget-check: Codex's review-15 probe (reproduktionen/patch40-budget-markers.cpp),
// adapted for patch 0041 (Claude Code): marker snapshots are booked at their real size; a refused waveform leaves a whole new state.
// Host probe for the SYNC editor budget with markers and for an allocation failure after publication.
#include "audio-engine.h"
#include "common/platform_memory.h"
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
#include <array>
#include <string>
#include <vector>
#include <unistd.h>
#include <cstdio>

namespace {
bool measure = false;
size_t failBytes = 0;
struct Allocation { void* pointer = nullptr; size_t bytes = 0; };
std::array<Allocation, 64> allocations{};
size_t liveBytes = 0;
size_t peakBytes = 0;
}

void* operator new(size_t bytes) {
    if (failBytes && bytes == failBytes) {
        failBytes = 0;
        throw std::bad_alloc{};
    }
    if (void* memory = std::malloc(bytes == 0 ? 1 : bytes)) {
        if (measure) {
            auto entry = std::find_if(allocations.begin(), allocations.end(),
                                      [](const Allocation& a) { return !a.pointer; });
            assert(entry != allocations.end());
            *entry = {memory, bytes};
            liveBytes += bytes;
            peakBytes = std::max(liveBytes, peakBytes);
        }
        return memory;
    }
    throw std::bad_alloc{};
}

void operator delete(void* memory) noexcept {
    if (measure && memory) {
        auto entry = std::find_if(allocations.begin(), allocations.end(),
                                  [memory](const Allocation& a) { return a.pointer == memory; });
        if (entry != allocations.end()) {
            liveBytes -= entry->bytes;
            *entry = {};
        }
    }
    std::free(memory);
}

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

void begin_measurement() {
    allocations.fill({});
    liveBytes = 0;
    peakBytes = 0;
    measure = true;
}

void measure_editor(const std::filesystem::path& root, int input, int fileMarkers, bool manual) {
    constexpr int target = 96000;
    constexpr int64_t budget = 3 * 1024 * 1024;
    Sample s(root, true, 48000, input, fileMarkers);
    auto& se = s.state.sampleEditor;
    assert(se.fileMarkers.size() == static_cast<size_t>(fileMarkers));
    if (manual) {
        se.selectionStart = 8; // ADD @S materialises the file list, on its first existing boundary
        s.tap(12, 0);
        assert(se.manualMarkers.size() == static_cast<size_t>(fileMarkers));
    }
    configure_sync(s, true, 2, 0);
    songcore::SampleSync sync;
    sync.kind = songcore::SampleSync::Kind::STRETCH;
    sync.frames = target;
    const auto engineBytes = songcore::sync_peak_bytes(input, 2, sync, 48000.f) + int64_t(input) * 8;
    assert(setenv("PT_LOAD_BUDGET_MB", "3", 1) == 0);
    assert(pt::load_budget_bytes() == budget);
    begin_measurement();
    s.tap(16, 2);
    measure = false;
    unsetenv("PT_LOAD_BUDGET_MB");
    const int64_t markerBytes = int64_t(fileMarkers) * 4 + (manual ? int64_t(fileMarkers) * 8 : 0);
    if (fileMarkers == 2048) {   // engine + Undo + the 8 KiB snapshot do not fit 3 MiB: refused before
        std::cout << input << " frames, 2048 file markers: refused [" << se.toolMessage << "], measured "
                  << peakBytes << ", needed " << engineBytes + markerBytes + 4096 << '\n';
        assert(se.totalFrames == input && se.toolMessage == "EDIT CANCELLED: UNDO MEMORY UNAVAILABLE");
        assert(engineBytes + markerBytes + 4096 > budget && peakBytes < static_cast<size_t>(budget));
        return;
    }
    assert(se.totalFrames == target && se.toolMessage.empty());
    assert(se.fileMarkers.size() == static_cast<size_t>(fileMarkers));
    assert(se.manualMarkers.size() == (manual ? static_cast<size_t>(fileMarkers) : 0));
    std::cout << input << " frames, " << fileMarkers << " file markers, " << se.manualMarkers.size()
              << " manual, engine+undo " << engineBytes << ", booked " << engineBytes + 4096
              << ", measured " << peakBytes << ", editor overhead "
              << static_cast<int64_t>(peakBytes) - engineBytes << ", budget " << budget << '\n';
    assert(peakBytes <= static_cast<size_t>(engineBytes + markerBytes + 4096) &&
           peakBytes <= static_cast<size_t>(budget));
}

void refresh_failure(const std::filesystem::path& root) {
    Sample s(root, true, 48000, frames, 256);
    auto& se = s.state.sampleEditor;
    configure_sync(s, true, 2, 0);
    const auto generation = s.host.sample_generation(s.slot);
    const int bins = pt::ui::SampleEditorModule::waveform_w();
    failBytes = bins * 2 * sizeof(float);
    bool escaped = false;
    try { s.tap(16, 2); } catch (const std::bad_alloc&) { escaped = true; }
    std::cout << "Waveform refresh refusal: exception " << escaped << ", generation +"
              << s.host.sample_generation(s.slot) - generation << ", engine length "
              << s.host.sample_length(s.slot) << ", editor length " << se.totalFrames
              << ", markers " << se.marker_count() << ", modified " << se.isModified << '\n';
    // The whole new state, only without its picture: markers rescaled, marked modified, said why.
    assert(!escaped && failBytes == 0 && se.totalFrames == 96000 && s.host.sample_length(s.slot) == 96000);
    assert(s.host.sample_generation(s.slot) == generation + 1);
    assert(se.fileMarkers.size() == 256 && se.isModified && se.undoMarkers.fileMarkers.size() == 256);
    assert(se.toolMessage == "WAVEFORM NOT SHOWN: NOT ENOUGH MEMORY" && se.waveformData.empty());
}
} // namespace

int main(int argc, char** argv) {
    assert(argc == 2);
    // A directory of its own, new for this run (mkdtemp), and only that is removed at the end.
    std::filesystem::create_directories(argv[1]);
    std::string tmpl = (std::filesystem::path(argv[1]) / "ptsample-editor-marker-budget.XXXXXX").string();
    assert(mkdtemp(tmpl.data()) != nullptr);
    const std::filesystem::path root = tmpl;
    measure_editor(root / "standard", 192000, 0, false);
    measure_editor(root / "file-256", 192000, 256, false);
    measure_editor(root / "manual-256", 192000, 256, true);
    measure_editor(root / "file-2048", 198750, 2048, false);
    refresh_failure(root / "refresh-failure");
    std::filesystem::remove_all(root);
    std::puts("PASS: marker snapshots are booked at their real size (256+256 fit 3 MiB, 2048 cues refused before), and a refused waveform after SYNC leaves the whole new state, marked modified");
}
