// VI: the visualizer across the main area. A+LEFT/RIGHT step the style (the same setting as SETTINGS >
// VISUALIZER); every other edit does nothing. R + D-pad, START and SELECT take the generic paths.

#include "ui/dispatch/dispatch_common.h"
#include "ui/modules/settings_editor.h"   // visualizer_names

namespace pt::ui {

GestureResult InputDispatcher::visualizer_screen(Gesture g) {
    switch (g) {
        case Gesture::A_LEFT:
        case Gesture::A_RIGHT: {
            const int count = static_cast<int>(SettingsModule::visualizer_names().size());
            const int step  = g == Gesture::A_RIGHT ? 1 : count - 1;   // wraps both ways
            s_.theme.visualizerType =
                static_cast<VisualizerType>((static_cast<int>(s_.theme.visualizerType) + step) % count);
            s_.vizNameUntilMs = static_cast<uint64_t>(now_ms_) + 1000;
            return GestureResult::TAKEN;
        }
        // No accidental edits: the cursor and value gestures land nowhere.
        case Gesture::DPAD_UP: case Gesture::DPAD_DOWN: case Gesture::DPAD_LEFT: case Gesture::DPAD_RIGHT:
        case Gesture::A: case Gesture::A_UP: case Gesture::A_DOWN: case Gesture::A_A: case Gesture::A_B:
        case Gesture::B: case Gesture::B_UP: case Gesture::B_DOWN: case Gesture::B_LEFT: case Gesture::B_RIGHT:
        case Gesture::L_A: case Gesture::L_B: case Gesture::L_B_A:
        case Gesture::L_UP: case Gesture::L_DOWN: case Gesture::L_LEFT: case Gesture::L_RIGHT:
            return GestureResult::TAKEN;
        default:
            return GestureResult::PASS;
    }
}

}  // namespace pt::ui
