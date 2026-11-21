#pragma once

// The confirm dialog: a question and "A=YES  B=NO" on a dimmed screen. No cursor — the buttons are
// the answer — except COLLECT_EXISTS's three, picked with LEFT/RIGHT. ⚠️ One state, not a flag per
// question: every handler tests `is_open()`.

#include <string>
#include <vector>

#include "ui/canvas.h"
#include "ui/theme.h"

namespace pt::ui {

struct ConfirmDialogState {
    enum class Kind {
        NONE,
        M8_IMPORT,
        CLEAN_SEQ,     // PROJECT → COMPACT → SEQ
        CLEAN_INST,    // PROJECT → COMPACT → INST
        NEW_PROJECT,   // PROJECT → NEW, when DIRTY
        LOAD_PROJECT,  // PROJECT → LOAD, when DIRTY: a load replaces the work and deletes its autosave
        CHANGE_TYPE,   // INSTRUMENT → TYPE, when the slot already has a source loaded
        EXIT,          // PROJECT → EXIT, when DIRTY (the shell only)

        // RECOVER WORK? at boot, after an autosave survived. ⚠️ The only B that does work: it must
        // delete the autosave, or the question returns every launch.
        RECOVER,

        COLLECT_EXISTS,   // PROJECT → COLLECT, when Songs/<name> is already there
        SAVE_EXISTS,      // PROJECT → SAVE, when Projects/<name>.ptp is another document's file
        FIRST_BEAT,       // help → START (First beat), when DIRTY: the tutorial starts a new project
        // A typed-name save onto a taken file: <THING> EXISTS - OVERWRITE?. `arg` is the save's
        // QwertyContext; A overwrites, B opens the keyboard on a -COPY name.
        SAVE_OVERWRITE,
        // A song refused as damaged but readable: A repairs and loads it. `arg` says where it came
        // from; B differs (LOAD keeps the open song, RESUME starts an empty one).
        REPAIR_SONG,
    };

    // REPAIR_SONG's way in.
    static constexpr int REPAIR_FROM_LOAD   = 0;
    static constexpr int REPAIR_FROM_RESUME = 1;

    // COLLECT_EXISTS's and SAVE_EXISTS's answers, left to right; `arg` is the one under the cursor.
    static constexpr int COLLECT_REPLACE  = 0;
    static constexpr int COLLECT_NEW_COPY = 1;
    static constexpr int COLLECT_CANCEL   = 2;

    std::vector<std::string> details;
    Kind kind = Kind::NONE;

    // What the answer needs beyond its Kind: CHANGE_TYPE's direction (±1), COLLECT_EXISTS's choice.
    // Reset on `close()`.
    int arg = 0;

    bool is_open() const     { return kind != Kind::NONE; }
    // REPLACE / NEW COPY / CANCEL, picked with LEFT/RIGHT, instead of A=YES B=NO.
    bool is_choice() const   { return kind == Kind::COLLECT_EXISTS || kind == Kind::SAVE_EXISTS; }
    void open(Kind k, int a = 0) { kind = k; arg = a; details.clear(); }
    void close()             { kind = Kind::NONE; arg = 0; }
};

std::string confirm_dialog_title(ConfirmDialogState::Kind kind, int arg = 0);

// The box itself, asking `title`; the sample editor uses it alone. ⚠️ It does not dim the screen —
// `draw_confirm_dialog` does.
void draw_confirm_box(Canvas& c, const std::string& title, const Theme& t);

void draw_confirm_dialog(Canvas& c, const ConfirmDialogState& s, const Theme& t);

}  // namespace pt::ui
