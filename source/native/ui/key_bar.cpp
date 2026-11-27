#include "ui/key_bar.h"

#include <string>

#include "ui/app_state.h"
#include "ui/canvas.h"
#include "ui/helpers.h"
#include "ui/theme.h"

namespace pt::ui {

namespace {

// Each screen's unique shortcuts, most useful first; at most KEY_BAR_MAX_ITEMS. Checked against the
// dispatcher (the shortcut audit of 2026-11-27).
constexpr KeyBarItem SONG_ITEMS[]       = {{"R+→", "chain"}, {"R+B", "mute"}, {"R+A", "solo"},
                                           {"B+←/→", "live"}};
constexpr KeyBarItem CHAIN_ITEMS[]      = {{"R+→", "phrase"}, {"R+←", "song"}, {"B+←/→", "track"},
                                           {"L+B", "mark"}};
constexpr KeyBarItem PHRASE_ITEMS[]     = {{"R+→", "inst"}, {"SELECT+↑/↓", "undo/redo"},
                                           {"SELECT+←/→", "overview"}, {"L+→", "fill"}};
constexpr KeyBarItem INSTRUMENT_ITEMS[] = {{"R+↑", "mods"}, {"R+→", "table"}, {"B+←/→", "next inst"},
                                           {"R+SELECT", "studio"}};
constexpr KeyBarItem TABLE_ITEMS[]      = {{"R+←", "inst"}, {"B+←/→", "table"}, {"A+START", "audition"},
                                           {"L+B", "mark"}};
constexpr KeyBarItem GROOVE_ITEMS[]     = {{"R+↓", "phrase"}, {"R+↑", "scale"}, {"B+←/→", "groove"},
                                           {"B+↑/↓", "qnt"}};
constexpr KeyBarItem SCALE_ITEMS[]      = {{"R+↓", "groove"}, {"B+←/→", "scale"}, {"R+←/→", "phrase"}};
constexpr KeyBarItem MODS_ITEMS[]       = {{"R+↓", "inst"}, {"R+↑", "pool"}, {"B+←/→", "next inst"},
                                           {"A+START", "audition"}};
constexpr KeyBarItem INST_POOL_ITEMS[]  = {{"R+→", "edit"}, {"R+↓", "mods"}, {"A+START", "audition"},
                                           {"B+↑/↓", "jump 16"}};
constexpr KeyBarItem MIXER_ITEMS[]      = {{"R+B", "mute"}, {"R+A", "solo"}, {"L+R", "all on"},
                                           {"R+↓", "effects"}};
constexpr KeyBarItem EFFECTS_ITEMS[]    = {{"R+↑", "mixer"}, {"R+←/→", "tracker"}};
constexpr KeyBarItem PROJECT_ITEMS[]    = {{"R+↓", "back"}, {"R+↑", "perform"}, {"R+←/→", "tracker"}};
constexpr KeyBarItem BACK_ITEMS[]       = {{"B", "back"}};   // SETTINGS, MIDI, MIDI_MAP
constexpr KeyBarItem PERFORM_ITEMS[]    = {{"B+←/→", "tile kind"}, {"L+A", "add track"},
                                           {"L+←/→", "track"}, {"R+↓", "project"}};
constexpr KeyBarItem VISUALIZER_ITEMS[] = {{"A+←/→", "style"}, {"R+→", "song"}};

template <int N>
const KeyBarItem* list(const KeyBarItem (&items)[N], int& count) {
    static_assert(N <= KEY_BAR_MAX_ITEMS, "a key bar holds at most KEY_BAR_MAX_ITEMS");
    count = N;
    return items;
}

}  // namespace

const KeyBarItem* key_bar_items(ScreenType screen, int& count) {
    switch (screen) {
        case ScreenType::SONG:       return list(SONG_ITEMS, count);
        case ScreenType::CHAIN:      return list(CHAIN_ITEMS, count);
        case ScreenType::PHRASE:     return list(PHRASE_ITEMS, count);
        case ScreenType::INSTRUMENT: return list(INSTRUMENT_ITEMS, count);
        case ScreenType::TABLE:      return list(TABLE_ITEMS, count);
        case ScreenType::GROOVE:     return list(GROOVE_ITEMS, count);
        case ScreenType::SCALE:      return list(SCALE_ITEMS, count);
        case ScreenType::MODS:       return list(MODS_ITEMS, count);
        case ScreenType::INST_POOL:  return list(INST_POOL_ITEMS, count);
        case ScreenType::MIXER:      return list(MIXER_ITEMS, count);
        case ScreenType::EFFECTS:    return list(EFFECTS_ITEMS, count);
        case ScreenType::PROJECT:    return list(PROJECT_ITEMS, count);
        case ScreenType::SETTINGS:
        case ScreenType::MIDI:
        case ScreenType::MIDI_MAP:   return list(BACK_ITEMS, count);
        case ScreenType::PERFORM:    return list(PERFORM_ITEMS, count);
        case ScreenType::VISUALIZER: return list(VISUALIZER_ITEMS, count);
        default:                     count = 0; return nullptr;
    }
}

bool key_bar_shown(const AppState& s) {
    if (!s.settings.keyBar || !s.project || s.studioOpen) return false;
    if (s.currentScreen == ScreenType::FILE_BROWSER || s.currentScreen == ScreenType::SAMPLE_EDITOR) return false;
    return true;
}

// The pixel font at half the editors' size: it has the arrows, and 10 px fits the 16 px bar.
constexpr int BAR_SCALE = 2, BAR_SPACING = 1;
int bar_width(const char* text) { return Canvas::text_width(text, BAR_SPACING, BAR_SCALE); }

void draw_key_bar(Canvas& c, const AppState& s, int x, int w) {
    const Theme& t = s.theme;
    const int y = DESIGN_H - KEY_BAR_H;
    c.fill_rect(x, y, w, KEY_BAR_H, t.background);
    c.fill_rect(x, y, w, 1, with_alpha(t.textParam, .18f));

    int count = 0;
    const KeyBarItem* items = key_bar_items(s.currentScreen, count);
    const int right = x + w - 4;
    // A key cap, then what it does, in the quiet colour.
    const auto item = [&](int at, const char* keys, const char* does) {
        const int keysW = bar_width(keys);
        c.panel_rect(at, y + 2, keysW + 6, KEY_BAR_H - 3, with_alpha(t.textValue, .12f),
                     with_alpha(t.textValue, .35f), 3);
        c.draw_text(keys, at + 3, y + 4, t.textTitle, BAR_SPACING, BAR_SCALE);
        c.draw_text(does, at + keysW + 10, y + 4, t.textEmpty, BAR_SPACING, BAR_SCALE);
        return at + keysW + 10 + bar_width(does) + 14;
    };
    // SELECT HELP always shows, on the right; the screen's items stop short of it.
    const int helpW = bar_width("SELECT") + 10 + bar_width("help");
    const int helpX = right - helpW;
    int at = x + 4;
    for (int i = 0; i < count; ++i) {
        if (at + bar_width(items[i].keys) + 10 + bar_width(items[i].does) > helpX - 8) break;
        at = item(at, items[i].keys, items[i].does);
    }
    item(helpX, "SELECT", "help");
}

}  // namespace pt::ui
