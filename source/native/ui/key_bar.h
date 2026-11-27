#pragma once

// The key bar (specs/help-and-lessons.md, phase B2): one thin row at the foot of every tracker screen with
// the few shortcuts unique to that screen, then SELECT HELP. The common keys (D-pad moves, A adds, A+B
// deletes, START plays) are left out: everyone knows them. SETTINGS > KEY BAR turns it off.

#include "ui/screen.h"

namespace pt::ui {

class Canvas;
struct AppState;

// "keys" names buttons as the help does ("R+→", "SELECT+←/→"); "does" is one or two words.
struct KeyBarItem {
    const char* keys;
    const char* does;
};

inline constexpr int KEY_BAR_H = 16;        // px, at the canvas's foot
inline constexpr int KEY_BAR_MAX_ITEMS = 4; // before SELECT HELP

// The screen's own items; `count` is set to how many (0 for a screen with none).
const KeyBarItem* key_bar_items(ScreenType screen, int& count);

// Whether the bar is drawn now: the setting, a tracker screen, nothing modal over it.
bool key_bar_shown(const AppState& s);

// The bar across [x, x + w) at the canvas's foot.
void draw_key_bar(Canvas& c, const AppState& s, int x, int w);

}  // namespace pt::ui
