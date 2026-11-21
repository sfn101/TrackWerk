#pragma once

// The global AUDIO DRIVERS overlay (L1 + R1): the outputs SETTINGS > AUDIO OUT switches between,
// reachable from any screen. The shell fills the list each frame; picking a row writes
// `SettingsValues::audioOutIndex`, which the frame loop's `sync_audio_output` turns into an actual
// switch (the same path the SETTINGS row uses, so the two cannot disagree).

#include <algorithm>
#include <string>
#include <vector>

#include "ui/canvas.h"
#include "ui/theme.h"

namespace pt::ui {

struct AudioDriversState {
    bool isOpen = false;
    int  cursor = 0;      // row under the cursor
    int  active = 0;      // the output playing now
    bool chordOwned = false;   // opened by L1+R1: swallow that chord's release (plain L+R is CANCEL)
    bool available = false;
    std::vector<std::string> names;
    std::string message;
    bool messageOk = true;

    // Refreshed by the shell every frame from AudioOutputSelector.
    void sync_outputs(const std::vector<std::string>& outputs, int activeIndex) {
        if (names != outputs) names = outputs;
        available = !names.empty();
        if (activeIndex >= 0 && activeIndex < static_cast<int>(names.size())) active = activeIndex;
        else active = 0;
        const int last = static_cast<int>(names.size()) - 1;
        if (cursor < 0) cursor = 0;
        if (cursor > last) cursor = last < 0 ? 0 : last;
    }

    bool is_on(int row) const { return cursor == row; }
};

void draw_audio_drivers(Canvas& c, const AudioDriversState& s, const Theme& t);

}  // namespace pt::ui
