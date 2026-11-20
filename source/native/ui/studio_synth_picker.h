#pragma once

#include "songcore/model.h"
#include "ui/buttons.h"
#include "ui/sound_browser.h"

#include <array>
#include <optional>
#include <string>
#include <vector>

class AudioEngine;
namespace pt::ui { class FileSystem; }
namespace songcore { class SongcoreHost; }

namespace pt::ui {

struct AppState;

enum class StudioSynthPickerMode { Closed, Model, Preset };

struct StudioSynthPickerState {
    StudioSynthPickerMode mode = StudioSynthPickerMode::Closed;
    int instrument = -1;
    int group = 0;
    int item = 0;
    bool chooseArmed = false;
    bool previewHeld = false;
    bool previewSounding = false;

    // PRESET mode is the sound browser: the MODEL tab (`item`, the model's sixteen presets) and the
    // factory-sound tabs, each remembering its own row.
    SoundTab tab = SoundTab::MODEL;
    std::array<int, kSoundTabCount> rows{};
    // A replaces a sample / SoundFont / granular slot only after a second A (no way back from that).
    bool confirmReplace = false;
    // The level from which the last previewed sound is at full volume (its own volume tops out at
    // 255); 0 until a factory sound was previewed.
    int previewCeiling = 0;
    // What A last used in this browser visit, and the slot as it was before the first use — B puts
    // it back (an empty or synth slot only). Closing any other way keeps the new sound.
    std::string usedKey;
    struct Original {
        songcore::Instrument instrument;
        std::optional<songcore::Table> table;
        int projectVersion = 0;
        std::vector<std::string> recent;   // the recent list before the first use
    };
    std::optional<Original> original;
};

struct StudioSynthModelGroup {
    const char* name;
    int firstModel;
    int count;
    // A group of models that are not neighbours in the stable ID range lists them here (`count` entries); the
    // IDs themselves never move. nullptr: the models firstModel … firstModel + count - 1.
    const int* models = nullptr;
};
inline int studio_synth_group_model(const StudioSynthModelGroup& g, int item) {
    return g.models ? g.models[item] : g.firstModel + item;
}
inline int studio_synth_group_index(const StudioSynthModelGroup& g, int model) {
    for (int i = 0; i < g.count; ++i) if (studio_synth_group_model(g, i) == model) return i;
    return -1;
}

// Sample Granular is a UI choice outside the stable oscillator ID range.
inline constexpr int kSampleGranularModel = -1;
const std::array<StudioSynthModelGroup, 12>& studio_synth_model_groups();
int studio_synth_model_group_count();
int studio_synth_model_group(int model);
std::string studio_synth_preset_label(int model, int variation);

bool studio_synth_picker_open(const AppState&);
bool studio_synth_open_picker_for_row(AppState&, songcore::SongcoreHost&);
void studio_synth_close_picker(AppState&);
bool studio_synth_picker_button(const ButtonEvent&, AppState&, songcore::SongcoreHost&, AudioEngine&, FileSystem&);

} // namespace pt::ui
