// ptlatency-row-check: SETTINGS > LATENCY (specs/latency-display.md). Its text, its place right under
// AUDIO OUT, shown on every platform, and never selected: the cursor walk steps over it.
// Builds with GCC and MSVC; no assert.
// usage: ptlatency-row-check [ignored]
#include "ui/modules/settings_editor.h"

#include <cstdio>
#include <string>

using namespace pt::ui;

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
    std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
    if (!ok) ++failures;
}
}  // namespace

int main() {
    // The text: one decimal, rounded; ">= " when only the buffer is known; "-" before audio runs.
    check(latency_text(512, 48000, true) == "10.7 MS", "512 frames at 48 kHz, measured: 10.7 MS");
    check(latency_text(512, 48000, false) == ">= 10.7 MS", "...buffer only: >= 10.7 MS");
    check(latency_text(441, 44100, true) == "10.0 MS", "441 at 44.1 kHz: 10.0 MS");
    check(latency_text(96, 48000, true) == "2.0 MS", "96 at 48 kHz: 2.0 MS");
    check(latency_text(0, 48000, false) == "-" && latency_text(512, 0, true) == "-", "nothing known yet: -");

    // Shown on every platform, AUDIO OUT or not, and drawn right under it.
    check(settings_row_visible(SettingsRow::LATENCY, PlatformCaps{}), "visible with no capabilities");
    check(settings_display_index(SettingsRow::LATENCY) == settings_display_index(SettingsRow::AUDIO_OUT) + 1,
          "drawn right under AUDIO OUT");
    int found = 0;
    for (SettingsRow r : SETTINGS_DISPLAY_ORDER) found += (r == SettingsRow::LATENCY);
    check(found == 1, "in the display order exactly once");

    // Not selectable: the cursor walk steps over it both ways, and it is never the first row.
    PlatformCaps all;
    all.audioOutputs = true;
    const int out = static_cast<int>(SettingsRow::AUDIO_OUT);
    const int next = settings_next_visible_row(out, +1, all);
    check(next != static_cast<int>(SettingsRow::LATENCY) &&
          settings_display_index(static_cast<SettingsRow>(next)) > settings_display_index(SettingsRow::LATENCY),
          "DOWN from AUDIO OUT jumps over LATENCY");
    check(settings_next_visible_row(next, -1, all) == out, "UP comes back to AUDIO OUT");
    bool landed = false;
    for (int r = 0, i = 0; i < SETTINGS_ROW_COUNT * 2; ++i)
        landed |= (r = settings_next_visible_row(r, +1, PlatformCaps{})) == static_cast<int>(SettingsRow::LATENCY);
    check(!landed && settings_first_visible_row(PlatformCaps{}) != static_cast<int>(SettingsRow::LATENCY),
          "never reached, even with no AUDIO OUT row");

    // Even if a row number pointed at it, A+D-PAD would change nothing.
    SettingsValues v;
    SettingsState s{v};
    s.cursorRow = static_cast<int>(SettingsRow::LATENCY);
    const SettingsModule m;
    Theme theme;
    const SettingsValues before = v;
    InputAction set;
    set.type = ActionType::SET_VALUE;
    set.value = 1;
    m.handle_input(v, theme, PlatformCaps{}, s.cursorRow, 1, set);
    check(v.audioOutIndex == before.audioOutIndex && v.helpMode == before.helpMode, "A+D-PAD changes nothing");

    std::printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
