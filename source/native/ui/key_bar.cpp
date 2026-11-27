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
constexpr KeyBarItem PHRASE_ITEMS[]     = {{"R+→", "instrument"}, {"SELECT+←/→", "overview"},
                                           {"SELECT+↑/↓", "undo/redo"}, {"B+←/→", "track"}};
constexpr KeyBarItem INSTRUMENT_ITEMS[] = {{"R+SELECT", "studio"}, {"R+↑", "pool"}, {"R+↓", "mods"},
                                           {"B+←/→", "instrument"}};
constexpr KeyBarItem TABLE_ITEMS[]      = {{"R+←", "instrument"}, {"L+B", "mark"}, {"L+A", "paste"}};
constexpr KeyBarItem MIXER_ITEMS[]      = {{"R+B", "mute"}, {"R+A", "solo"}, {"L+R", "all on"}};

}  // namespace

const KeyBarItem* key_bar_items(ScreenType screen, int& count) {
    const auto list = [&](const KeyBarItem* items, int n) { count = n; return items; };
    switch (screen) {
        case ScreenType::SONG:       return list(SONG_ITEMS, 4);
        case ScreenType::CHAIN:      return list(CHAIN_ITEMS, 4);
        case ScreenType::PHRASE:     return list(PHRASE_ITEMS, 4);
        case ScreenType::INSTRUMENT: return list(INSTRUMENT_ITEMS, 4);
        case ScreenType::TABLE:      return list(TABLE_ITEMS, 3);
        case ScreenType::MIXER:      return list(MIXER_ITEMS, 3);
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
