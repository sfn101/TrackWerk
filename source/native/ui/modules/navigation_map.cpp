#include "ui/modules/navigation_map.h"

#include "ui/helpers.h"
#include "ui/navigation.h"

namespace pt::ui {

void NavigationMapModule::draw(Canvas& c, int x, int y, const NavigationMapState& s) const {
    const Theme& t = s.theme;

    c.fill_rect(x, y, WIDTH, HEIGHT, t.background);

    // A shared screen uses the column it was entered from.
    const int screenCol  = screen_column(s.currentScreen);
    const int currentCol = (screenCol == -1) ? s.sourceColumn : screenCol;

    // The always-visible main row, plus this column's own screens.
    const int col = (currentCol < 0 || currentCol >= NAV_COLUMNS) ? 2 : currentCol;
    std::optional<ScreenType> grid[NAV_ROWS][NAV_COLUMNS] = {};
    // Five columns at a time. VI is one column left of the grid: on it the main row slides right one
    // (V S C P I) and nothing sits above or below; elsewhere the usual S C P I T.
    const bool onVisualizer = s.currentScreen == ScreenType::VISUALIZER;
    if (onVisualizer) {
        grid[NAV_MAIN_ROW][0] = ScreenType::VISUALIZER;
        for (int gcol = 1; gcol < NAV_COLUMNS; ++gcol)
            grid[NAV_MAIN_ROW][gcol] = SCREEN_GRID[NAV_MAIN_ROW][gcol - 1];
    } else {
        for (int gcol = 0; gcol < NAV_COLUMNS; ++gcol)
            grid[NAV_MAIN_ROW][gcol] = SCREEN_GRID[NAV_MAIN_ROW][gcol];
        for (int row = 0; row < NAV_ROWS; ++row) grid[row][col] = SCREEN_GRID[row][col];
    }

    // The pool's INSTRUMENT cell (row 0 / col 4); on an INSTRUMENT reached from the pool it is the
    // current position.
    const bool onPoolInstrument = (s.currentScreen == ScreenType::INSTRUMENT && s.instrumentFromPool);
    if (s.currentScreen == ScreenType::INST_POOL || onPoolInstrument)
        grid[0][4] = ScreenType::INSTRUMENT;

    for (int row = 0; row < NAV_ROWS; ++row) {
        for (int gcol = 0; gcol < NAV_COLUMNS; ++gcol) {
            if (!grid[row][gcol]) continue;

            const ScreenType screen = *grid[row][gcol];
            const int        cellX  = x + (gcol * CELL_WIDTH);
            const int        cellY  = y + (row * CELL_HEIGHT);

            const bool isCurrent = onPoolInstrument ? (row == 0 && gcol == 4)
                                                    : (screen == s.currentScreen);

            // Main row: one large letter. Above and below: two smaller letters, a branch, not a peer.
            const bool        mainRow = row == NAV_MAIN_ROW;
            const int         scale   = mainRow ? FONT_SCALE : 2;
            const int         spacing = mainRow ? CHAR_SPACING : 1;   // two letters read as one word
            const std::string label   = screen_map_label(screen, mainRow);
            const int         labelW  = Canvas::text_width(label, spacing, scale);

            if (isCurrent) {
                c.fill_rect(cellX + 1, cellY + 1, CELL_WIDTH - 2, CELL_HEIGHT - 2,
                            with_alpha(t.rowCursor, .18f));
                c.fill_rect(cellX + 4, cellY + CELL_HEIGHT - 2, CELL_WIDTH - 8, 1, t.rowCursor);
                c.mark_selector(1, cellX + 1, cellY + 1, CELL_WIDTH - 2, CELL_HEIGHT - 2);   // glides (ui/layout.cpp)
            }

            c.draw_text(label, cellX + (CELL_WIDTH - labelW) / 2, cellY + (mainRow ? 3 : 6),
                        isCurrent ? cursor_mark_ink(t) : t.textParam, spacing, scale);
        }
    }

    // A column beyond the edge (VI left of S, or T right of I while on VI): a small arrow on that side
    // of the main row, so the map says there is more that way.
    {
        const int midY = y + NAV_MAIN_ROW * CELL_HEIGHT + CELL_HEIGHT / 2;
        const Argb ink = with_alpha(t.textParam, .8f);
        for (int i = 0; i < 3; ++i) {   // a 3-px chevron on the map's very edge, clear of the letter
            const int ax = onVisualizer ? x + WIDTH - 1 - i : x + i;
            c.fill_rect(ax, midY - i, 1, 2 * i + 1, ink);
        }
    }

    // Arrival banner across the top of the map: where the last move landed, in words.
    if (s.bannerLevel > 0.f) {
        const float a = s.bannerLevel;
        const std::string name = screen_long_label(s.currentScreen);
        c.fill_rect(x + 1, y + 1, WIDTH - 2, 19, with_alpha(t.background, .92f * a));
        c.fill_rect(x + 1, y + 1, WIDTH - 2, 1, with_alpha(t.rowCursor, a));
        c.fill_rect(x + 1, y + 19, WIDTH - 2, 1, with_alpha(t.rowCursor, .55f * a));
        c.draw_label(name, x + (WIDTH - Canvas::label_width(name, 12)) / 2, y + 3,
                     with_alpha(cursor_mark_ink(t), a), 12);
    }
}

}  // namespace pt::ui
