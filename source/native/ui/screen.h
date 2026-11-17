#pragma once

// The screens and their drawn labels. Grid positions are in ui/navigation.h.

#include <string>

namespace pt::ui {

enum class ScreenType {
    // Main screens (middle row — always visible)
    SONG,
    CHAIN,
    PHRASE,
    INSTRUMENT,
    TABLE,

    // Context screens — off the main row (ui/navigation.h)
    PROJECT,
    GROOVE,
    SCALE,
    MODS,
    INST_POOL,
    MIXER,      // shared by all columns
    EFFECTS,    // shared by all columns

    // Popup screens — replace the main view temporarily
    FILE_BROWSER,
    SETTINGS,
    SAMPLE_EDITOR,

    // Appended; screens are named, never numbered.
    MIDI,       // reached from PROJECT > MIDI; not on the R+DPAD grid
    MIDI_MAP,   // …and from MIDI > MAPPING. Not on the grid either
    PERFORM,    // live controls and CPU per track (ui/perform.h); row 0 of the grid, columns 0, 1, 4
    VISUALIZER  // the visualizer across the main area; left of SONG, off the grid (ui/navigation.h)
};

inline const char* screen_label(ScreenType s) {
    switch (s) {
        case ScreenType::SONG:          return "SONG";
        case ScreenType::CHAIN:         return "CHAIN";
        case ScreenType::PHRASE:        return "PHRASE";
        case ScreenType::INSTRUMENT:    return "INSTRUMENT";
        case ScreenType::TABLE:         return "TABLE";
        case ScreenType::PROJECT:       return "PROJECT";
        case ScreenType::GROOVE:        return "GROOVE";
        case ScreenType::SCALE:         return "SCALE";
        case ScreenType::MODS:          return "MODS";
        case ScreenType::INST_POOL:     return "INST.POOL";
        case ScreenType::MIXER:         return "MIXER";
        case ScreenType::EFFECTS:       return "EFFECTS";
        case ScreenType::FILE_BROWSER:  return "FILE BROWSER";
        case ScreenType::SETTINGS:      return "SETTINGS";
        case ScreenType::SAMPLE_EDITOR: return "SAMPLE EDITOR";
        case ScreenType::MIDI:          return "MIDI";
        case ScreenType::MIDI_MAP:      return "MIDI MAPPING";
        case ScreenType::PERFORM:       return "PERFORM";
        case ScreenType::VISUALIZER:    return "VISUALIZER";
    }
    return "";
}

// The navigation map's one-letter label. Not unique (SCALE and SONG are both "S"): the column tells.
inline const char* screen_short_label(ScreenType s);

// The full name, for the navigation map's arrival banner.
inline const char* screen_long_label(ScreenType s) {
    switch (s) {
        case ScreenType::SONG:          return "SONG";
        case ScreenType::CHAIN:         return "CHAIN";
        case ScreenType::PHRASE:        return "PHRASE";
        case ScreenType::INSTRUMENT:    return "INSTRUMENT";
        case ScreenType::TABLE:         return "TABLE";
        case ScreenType::PROJECT:       return "PROJECT";
        case ScreenType::GROOVE:        return "GROOVE";
        case ScreenType::SCALE:         return "SCALE";
        case ScreenType::MODS:          return "MODS";
        case ScreenType::INST_POOL:     return "INST. POOL";
        case ScreenType::MIXER:         return "MIXER";
        case ScreenType::EFFECTS:       return "EFFECTS";
        case ScreenType::FILE_BROWSER:  return "BROWSER";
        case ScreenType::SETTINGS:      return "SETTINGS";
        case ScreenType::SAMPLE_EDITOR: return "SAMPLE EDITOR";
        case ScreenType::MIDI:          return "MIDI";
        case ScreenType::MIDI_MAP:      return "MIDI MAP";
        case ScreenType::PERFORM:       return "PERFORM";
        case ScreenType::VISUALIZER:    return "VISUALIZER";
    }
    return "";
}

// Off the main row the map uses two letters, so no letter means two screens (S was SONG and SCALE,
// P was PHRASE, PROJECT and the POOL). The main row keeps the familiar single letters.
inline const char* screen_map_label(ScreenType s, bool mainRow) {
    if (mainRow) return screen_short_label(s);
    switch (s) {
        case ScreenType::SCALE:      return "SC";
        case ScreenType::INST_POOL:  return "PL";
        case ScreenType::PROJECT:    return "PR";
        case ScreenType::GROOVE:     return "GR";
        case ScreenType::MODS:       return "MO";
        case ScreenType::MIXER:      return "MX";
        case ScreenType::EFFECTS:    return "FX";
        case ScreenType::PERFORM:    return "PF";
        case ScreenType::INSTRUMENT: return "IN";   // the pool's instrument cell
        default:                     return screen_short_label(s);
    }
}

inline const char* screen_short_label(ScreenType s) {
    switch (s) {
        case ScreenType::SONG:          return "S";
        case ScreenType::CHAIN:         return "C";
        case ScreenType::PHRASE:        return "P";
        case ScreenType::INSTRUMENT:    return "I";
        case ScreenType::TABLE:         return "T";
        case ScreenType::PROJECT:       return "P";
        case ScreenType::GROOVE:        return "G";
        case ScreenType::SCALE:         return "S";
        case ScreenType::MODS:          return "M";
        case ScreenType::INST_POOL:     return "P";
        case ScreenType::MIXER:         return "V";
        case ScreenType::EFFECTS:       return "X";
        case ScreenType::FILE_BROWSER:  return "FB";
        case ScreenType::SETTINGS:      return "SE";
        case ScreenType::SAMPLE_EDITOR: return "SE";
        case ScreenType::MIDI:          return "MI";
        case ScreenType::MIDI_MAP:      return "MM";
        case ScreenType::PERFORM:       return "PF";
        case ScreenType::VISUALIZER:    return "V";   // MIXER's "V" is never drawn: off the main row it is MX
    }
    return "";
}

}  // namespace pt::ui
