// NEW and LOAD, export and resample.

#include "ui/dispatch/dispatch_common.h"

#include "ui/lifecycle.h"        // the crash-recovery autosave — write / clear / load
#include "ui/song_pointer.h"
#include "ui/source_sampler.h"
#include "ui/sample_granular_ui.h"

#include "songcore/render.h"   // project_file_name
#include "common/load_progress.h"   // load_cancelled
#include "ui/navigation.h"      // go_to_screen

#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace pt::ui {

/**
 * Back to zero on NEW and LOAD, or INSTRUMENT sits on a slot never chosen and A,A on SONG inserts a
 * chain number from the previous song.
 */
void InputDispatcher::reset_editing_context() {
    phraseHistory_.clear();
    seqHistory_.clear();
    s_.harmony = {};
    phraseArrow_.phraseId = -1;
    phraseRotation_.phraseId = -1;
    // NEW/LOAD may reuse the host Project address and reset its edit version to
    // zero. A pending asynchronous source take belongs to the previous song.
    if(s_.sourceSampler.openWhenReady||s_.sourceSampler.importRequested)source_sampler_stop(s_.sourceSampler);
    s_.sampleGranular.cropReturn=false;s_.sampleGranular.cropInstrument=-1;s_.sampleGranular.cropProject=nullptr;
    sample_granular_close_preset_picker(s_);
    s_.studioSynthCompare={};
    s_.currentPhrase = s_.currentChain = s_.currentInstrument = 0;
    s_.currentTable  = s_.currentGroove = 0;
    s_.lastEditedPhrase = s_.lastEditedChain = s_.lastEditedTable = 0;
    s_.lastEditedInstrument = s_.lastEditedTranspose = 0;
    s_.lastEditedNote   = songcore::Note::C4();
    s_.lastEditedVolume = 0x7F;

    // …and every secondary screen's own cursor, or they point into the previous song after a LOAD.
    s_.cursorRow = 0; s_.cursorColumn = 1;
    s_.songScrollPosition = 0;
    s_.instrumentCursorRow = 0; s_.instrumentCursorColumn = 1;
    // ⚠️ Both halves, or a master-only row over a track column shows no cursor.
    s_.mixerCursorColumn = 0;
    s_.mixerMasterRow    = 0;
    s_.effectsCursorRow  = 0;
    s_.tableCursorRow = 0; s_.tableCursorColumn = 1;
    s_.grooveCursorRow = 0; s_.grooveCursorColumn = GROOVE_COL_TICK;
    s_.groovePanelRow = 0;  s_.groovePanelColumn  = 0;
    // The quantize pointer is an editing aid, not a setting, so it resets with the project.
    s_.grooveQuantize = 0;
    s_.modCursorRow = 0; s_.modCursorPair = 0; s_.modCursorSide = 0;
    s_.projectCursorRow = 0; s_.projectCursorColumn = 1;

    // The REMEMBER slots, or REMEMBER mode restores a cursor from the previous song.
    s_.songCursorRow = 0;   s_.songCursorColumn = 1;
    s_.chainCursorRow = 0;  s_.chainCursorColumn = 1;
    s_.phraseCursorRow = 0; s_.phraseCursorColumn = 1;

    // ⚠️ Not the SETTINGS cursor and not poolCursorColumn: both persist on purpose.
    s_.selection = Selection{};

    // ⚠️ Under NAV = SONG those slots are the pointer, and row 0 / track 1 may be empty.
    clamp_song_pointer(s_);
}

void InputDispatcher::start_new_project() {
    // Another project: a running First beat ends (begin_first_beat starts its own afterwards).
    s_.firstBeat = FirstBeat{};
    cancel_midi_test();   // no test note outlives the project it was sent from
    host_.new_project();

    // A blank document: clean and pathless, or the next NEW or EXIT asks about unsaved work.
    s_.projectVersion      = 0;
    s_.savedProjectVersion = 0;
    s_.projectPath.clear();
    s_.repairedUnsaved     = false;

    // …and nothing to recover: the autosave and its pending deadline go too.
    autosavePending_ = false;
    autosave_clear(fs_);

    reset_editing_context();

    set_status(s_, "NEW PROJECT", true);
}

// A project name no saved project has, or "" when FIRSTBEAT … FIRSTBEAT9999 are all taken: SAVE writes
// Projects/<name>.ptp and would replace an existing file silently.
static std::string free_first_beat_name(FileSystem& fs) {
    const auto taken = [&fs](const std::string& n) {
        return fs.file_exists(fs.projects_directory() + "/" + songcore::project_file_name(n) + ".ptp");
    };
    if (!taken("FIRSTBEAT")) return "FIRSTBEAT";
    for (int n = 2; n <= 9999; ++n)
        if (!taken("FIRSTBEAT" + std::to_string(n))) return "FIRSTBEAT" + std::to_string(n);
    return "";
}

void InputDispatcher::toggle_first_beat() {
    on_help_dismiss();
    if (s_.firstBeat.active) {
        s_.firstBeat = FirstBeat{};
        set_status(s_, "FIRST BEAT CLOSED", true);
        return;
    }
    // Refused before the confirm, so a NO is never needed to keep a recording.
    if (const char* why = first_beat_blocked(s_)) {
        set_status(s_, why, false);
        first_beat_tell_blocked(s_, why);   // also in the Sample Editor's own line
        return;
    }
    if (free_first_beat_name(fs_).empty()) {
        set_status(s_, "FIRST BEAT: NO FREE PROJECT NAME", false);
        return;
    }
    if (s_.project_dirty()) s_.confirm.open(ConfirmDialogState::Kind::FIRST_BEAT);
    else begin_first_beat();
}

void InputDispatcher::begin_first_beat() {
    // Asked again: the confirm may have stood open while something started.
    if (const char* why = first_beat_blocked(s_)) {
        set_status(s_, why, false);
        first_beat_tell_blocked(s_, why);   // also in the Sample Editor's own line
        return;
    }
    const std::string name = free_first_beat_name(fs_);
    if (name.empty()) {
        set_status(s_, "FIRST BEAT: NO FREE PROJECT NAME", false);
        return;
    }
    start_new_project();
    host_.edit_project().name = name;   // a fresh project's name; it stays clean until the first edit
    // It begins where a song begins: on SONG, with the Studio and every dialog put away.
    s_.studioOpen = false;
    s_.studioHelp = false;
    on_help_dismiss();
    go_to_screen(s_, {ScreenType::SONG, 0});
    s_.firstBeat = FirstBeat{};
    s_.firstBeat.active = true;
    s_.firstBeat.projectName = name;
    first_beat_snapshot(s_);
    set_status(s_, "FIRST BEAT: " + name, true);
}

/** A .ptp just replaced the document. Leave the browser, and forget everything about the last one. */
void InputDispatcher::load_project_done(const std::string& path) {
    // Clean, with nothing to recover. ⚠️ Autosave recovery is the one load that is neither.
    s_.projectVersion      = 0;
    s_.savedProjectVersion = 0;
    s_.projectPath         = path;
    s_.repairedUnsaved     = false;   // repair_pending_song sets it again after this
    ++s_.projectLoads;
    cancel_midi_test();
    // A running First beat stays only when its own saved file came back (its RELOAD step); any
    // other project ends it, so the card never guides a stranger's song.
    if (s_.firstBeat.active &&
        path != fs_.projects_directory() + "/" + songcore::project_file_name(s_.firstBeat.projectName) + ".ptp")
        s_.firstBeat = FirstBeat{};

    autosavePending_ = false;   // …or it fires 3 s from now and re-creates the file this just deleted
    autosave_clear(fs_);

    reset_editing_context();

    close_file_browser();

    // ⚠️ The only report of a sample that did not load (no console on the devices), out of memory too.
    const int failed = host_.last_media_load().failed;
    set_status(s_, failed > 0 ? "LOADED: " + std::to_string(failed) + " MISSING" : "LOADED", (failed == 0));
}

void InputDispatcher::repair_pending_song() {
    const std::string path = s_.repairPendingPath;
    s_.repairPendingPath.clear();
    if (path.empty()) return;

    // The same strip and cancel as a browser load.
    const LoadScope loadScope(*this, now_ms_, path_stem(path));
    if (!host_.load_project_file(path, fs_.samples_directory(), /*repair=*/true)) {
        set_status(s_.fileBrowser, "LOAD FAILED", false);
        return;
    }
    // ⚠️ As in the browser: a cancelled load cannot stay half-done.
    if (pt::load_cancelled()) {
        host_.new_project();
        host_.push_params();
        load_project_done("");
        set_status(s_, "LOAD CANCELLED", true);
        return;
    }
    const int fixes = host_.last_repair_count();
    load_project_done(path);
    // Unsaved on purpose: the file on disk is still the damaged one, and the first SAVE asks.
    mark_modified();
    s_.repairedUnsaved = true;
    set_status(s_, repaired_status(fixes), true);
}

// ─── EXPORT ──────────────────────────────────────────────────────────────────────────────────────

void InputDispatcher::export_song(bool stems) {
    if (s_.isRendering) return;   // a second press while one runs is a mis-press, not a request

    host_.stop();                                    // the session ends; a render is not playback
    if (render_.suspend_audio) render_.suspend_audio(true);

    s_.isRendering    = true;
    s_.renderProgress = 0.0f;
    set_status(s_, stems ? "RENDERING STEMS..." : "RENDERING...", true);
    if (render_.repaint) render_.repaint();          // …so the message is on screen before we block

    const auto progress = [this](float p) {
        s_.renderProgress = p;
        if (render_.repaint) render_.repaint();      // the EXPORT row's "43%" — a readout, not a decoration
    };

    // The panel's rows, with AUTO resolved against the project as it stands right now.
    RenderRange range;
    range.startRow = s_.renderDialog.startRow;
    range.endRow   = render_dialog_end_row(s_.renderDialog, host_.project());
    range.repeat   = s_.renderDialog.repeat;

    const ActionResult r = stems ? render_stems(host_, fs_, s_, range, progress)
                                 : render_mix(host_, fs_, s_, range, progress);

    s_.isRendering    = false;
    s_.renderProgress = 0.0f;
    set_status(s_, r.message, r.ok);

    if (render_.suspend_audio) render_.suspend_audio(false);
}

// ─── COLLECT ─────────────────────────────────────────────────────────────────────────────────────

void InputDispatcher::collect_song(const std::string& folder) {
    if (s_.isCollecting) return;

    s_.isCollecting    = true;
    s_.collectProgress = 0.0f;
    set_status(s_, "COLLECTING...", true);
    if (render_.repaint) render_.repaint();

    const auto progress = [this](float p) {
        s_.collectProgress = p;
        if (render_.repaint) render_.repaint();
    };
    const ActionResult r = collect_project(host_, fs_, folder, progress);

    s_.isCollecting    = false;
    s_.collectProgress = 0.0f;
    set_status(s_, r.message, r.ok);
}

void InputDispatcher::resample_selection(const std::string& customBaseName) {
    if (s_.isRendering) return;         // a render is already running — a second APPLY is a mis-press
    if (!s_.selection.active) return;   // the selection lapsed between opening the keyboard and APPLY

    // The selected TRACKS: SONG column − 1, clamped to the eight that exist.
    const SelectionBounds b = s_.selection.bounds();
    std::set<int> tracks;
    for (int c = b.topLeftColumn - 1; c <= b.bottomRightColumn - 1; ++c)
        if (c >= 0 && c <= 7) tracks.insert(c);
    if (tracks.empty()) return;

    // export_song's shape.
    host_.stop();
    if (render_.suspend_audio) render_.suspend_audio(true);

    s_.isRendering    = true;
    s_.renderProgress = 0.0f;
    set_status(s_, "RESAMPLING...", true);
    if (render_.repaint) render_.repaint();

    const auto progress = [this](float p) {
        s_.renderProgress = p;
        if (render_.repaint) render_.repaint();
    };

    std::string        outPath;
    const ActionResult r = render_resample(host_, fs_, b.topLeftRow, b.bottomRightRow, tracks,
                                           customBaseName, outPath, progress);

    s_.isRendering    = false;
    s_.renderProgress = 0.0f;

    if (r.ok) {
        const int instId = create_resampled_instrument(host_, outPath);
        if (instId >= 0) {
            char msg[40];
            // ASCII arrow: the 5×5 font has no →.
            std::snprintf(msg, sizeof(msg), "RESAMPLED -> INST %02X", instId);
            set_status(s_, msg, true);
            mark_modified();   // a new instrument is unsaved work
        } else {
            // The WAV rendered but no slot could take it (pool full, or the file will not reload).
            set_status(s_, "NO FREE INSTRUMENT", false);
        }
    } else {
        set_status(s_, r.message, false);   // "RESAMPLE FAILED"
    }

    if (render_.suspend_audio) render_.suspend_audio(false);
}

}  // namespace pt::ui
