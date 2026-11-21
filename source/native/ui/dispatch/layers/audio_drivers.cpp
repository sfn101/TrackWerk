// The global AUDIO DRIVERS overlay (L1 + R1): pick the output route from anywhere. The list and the
// active row come from the shell (AudioOutputSelector); A only writes `settings.audioOutIndex`, the
// same field SETTINGS > AUDIO OUT edits, so one switch in the frame loop owns the actual change.

#include "ui/dispatch/dispatch_common.h"

namespace pt::ui {

GestureResult InputDispatcher::audio_drivers_layer(Gesture g) {
    AudioDriversState& a = s_.audioDrivers;
    switch (g) {
        case Gesture::DPAD_UP:
            if (!a.names.empty()) a.cursor = std::max(0, a.cursor - 1);
            a.message.clear();
            break;
        case Gesture::DPAD_DOWN:
            if (!a.names.empty())
                a.cursor = std::min(static_cast<int>(a.names.size()) - 1, a.cursor + 1);
            a.message.clear();
            break;
        case Gesture::A:
            if (!a.names.empty()) {
                s_.settings.audioOutIndex = a.cursor;
                a.message = "SWITCHING...";
                a.messageOk = true;
            }
            break;
        case Gesture::B:
            close_audio_drivers();
            break;
        // The chord's own release: consumed, and the plain L+R CANCEL it would otherwise run is not.
        // Reached with the overlay already closed too, because the release comes after B.
        case Gesture::L_R:
            a.chordOwned = false;
            break;
        default:
            break;
    }
    return GestureResult::TAKEN;
}

void InputDispatcher::open_audio_drivers() {
    // A dialog or editor on top owns the buttons; the chord does not reach through it.
    if (const Layer* top = top_layer(); top && top->handle != &InputDispatcher::audio_drivers_layer)
        return;

    AudioDriversState& a = s_.audioDrivers;
    a.isOpen = true;
    a.chordOwned = true;
    a.message.clear();
    a.messageOk = true;
    a.cursor = a.active;
}

}  // namespace pt::ui
