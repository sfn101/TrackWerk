// The QWERTY keyboard, over whatever raised it (the theme editor's SAVE raises it over the editor).

#include "ui/dispatch/dispatch_common.h"

#include "ui/std_filesystem.h"   // path_stem / path_extension / path_sanitize
#include "ui/source_sampler.h"
#include "ui/fx_preset_bank.h"   // reverb/delay_preset_display_name
#include "ui/fx_preset_io.h"     // REVERB/DELAY_FILE_EXT, FX_PRESET_NAME_MAX
#include "ui/groove_io.h"        // GROOVE_FILE_EXT
#include "ui/scale_io.h"         // SCALE_FILE_EXT
#include "ui/project_actions.h"  // project_save_path / save_would_replace_other_song

#include <string>

namespace pt::ui {

namespace {

// The path FILE_RENAME would create (StdFileSystem::rename_file's rule), to tell a taken name apart.
std::string rename_target(FileSystem& fs, const std::string& path, const std::string& name) {
    const std::string ext = fs.is_directory(path) ? std::string() : path_extension(path);
    std::string file = path_sanitize(name, /*allow_dot=*/true);
    if (file.empty()) return std::string();
    const std::string suffix = "." + ext;
    if (!ext.empty() && !(file.size() > suffix.size() &&
                          file.compare(file.size() - suffix.size(), suffix.size(), suffix) == 0))
        file += suffix;
    return fs.parent_path(path) + "/" + file;
}

// The keyboard a typed-name save reopens with: its label and length.
struct SaveKeyboard { const char* label; int maxLength; };
SaveKeyboard save_keyboard(QwertyContext c) {
    switch (c) {
        case QwertyContext::INSTRUMENT_SAVE: return {"SAVE PRESET:", 20};
        case QwertyContext::THEME_SAVE:      return {"SAVE THEME:", 20};
        case QwertyContext::SCALE_SAVE:      return {"SAVE SCALE:", 20};
        case QwertyContext::GROOVE_SAVE:     return {"SAVE GROOVE:", 20};
        case QwertyContext::REVERB_SAVE:     return {"SAVE REVERB:", FX_PRESET_NAME_MAX};
        case QwertyContext::DELAY_SAVE:      return {"SAVE DELAY:", FX_PRESET_NAME_MAX};
        default:                             return {"SAVE COPY:", PROJECT_NAME_MAX_CHARS};
    }
}

}  // namespace

std::string InputDispatcher::typed_save_path(QwertyContext c, const std::string& dir, const std::string& text) {
    const auto file = [&](const std::string& name, const char* fallback, const char* ext) {
        const std::string safe = safe_file_name(name);
        return dir + "/" + (safe.empty() ? std::string(fallback) : safe) + "." + ext;
    };
    const songcore::Project& p = host_.project();
    switch (c) {
        case QwertyContext::INSTRUMENT_SAVE: return file(text, "PRESET", "pti");
        case QwertyContext::THEME_SAVE:      return file(text, "THEME", "ptt");
        case QwertyContext::SCALE_SAVE:      return file(text, "SCALE", SCALE_FILE_EXT);
        case QwertyContext::GROOVE_SAVE:     return file(text, "GROOVE", GROOVE_FILE_EXT);
        // An empty field saves under the name the row shows.
        case QwertyContext::REVERB_SAVE:
            return file(text.empty() ? reverb_preset_display_name(p) : text, "REVERB", REVERB_FILE_EXT);
        case QwertyContext::DELAY_SAVE:
            return file(text.empty() ? delay_preset_display_name(p) : text, "DELAY", DELAY_FILE_EXT);
        case QwertyContext::PROJECT_SAVE_AS: return project_save_path(host_, fs_, s_, text);
        default:                             return std::string();
    }
}

void InputDispatcher::typed_save(QwertyContext c, const std::string& dir, const std::string& text) {
    const std::string path = typed_save_path(c, dir, text);
    // A song's own file never asks; a preset always does (presets don't remember their file).
    const bool taken = c == QwertyContext::PROJECT_SAVE_AS ? save_would_replace_other_song(fs_, s_, path)
                                                           : fs_.file_exists(path);
    if (!taken) {
        run_typed_save(c, dir, text);
        return;
    }
    s_.pendingSave = AppState::PendingSave{c, dir, text};
    s_.confirm.open(ConfirmDialogState::Kind::SAVE_OVERWRITE, static_cast<int>(c));
}

void InputDispatcher::open_copy_keyboard(QwertyContext c, const std::string& dir, const std::string& text) {
    const SaveKeyboard kb = save_keyboard(c);
    const std::string base = text.empty() ? path_stem(typed_save_path(c, dir, text)) : text;
    // Only the suggestion is numbered; a typed name that is taken asks again.
    std::string name;
    for (int n = 1; n < 10000; ++n) {
        const std::string suffix = n == 1 ? std::string("-COPY") : "-COPY-" + std::to_string(n);
        const size_t room = static_cast<size_t>(kb.maxLength) > suffix.size() ? kb.maxLength - suffix.size() : 0;
        name = base.substr(0, room) + suffix;
        if (!fs_.file_exists(typed_save_path(c, dir, name))) break;
    }
    open_qwerty(c, name, kb.label, dir, kb.maxLength, /*clear_on_first_b=*/true);
}

void InputDispatcher::run_typed_save(QwertyContext c, const std::string& dir, const std::string& text) {
    switch (c) {
        case QwertyContext::INSTRUMENT_SAVE: {
            const std::string name = text.empty() ? "PRESET" : text;
            if (!save_instrument_preset(host_, fs_, s_.currentInstrument, typed_save_path(c, dir, text))) {
                set_status(s_, "SAVE FAILED", false);
                break;
            }
            // The slot takes the name it was saved under.
            Instrument& ins = host_.edit_project().instruments[static_cast<size_t>(s_.currentInstrument)];
            if (!text.empty() && ins.name != text) {
                ins.name = text;
                mark_modified();
            }
            set_status(s_, "SAVED: " + name, true);
            break;
        }
        case QwertyContext::THEME_SAVE:      save_theme_as(dir, text);         break;
        case QwertyContext::SCALE_SAVE:      save_scale_as(dir, text);         break;
        case QwertyContext::GROOVE_SAVE:     save_groove_as(dir, text);        break;
        case QwertyContext::REVERB_SAVE:     save_reverb_preset_as(dir, text); break;
        case QwertyContext::DELAY_SAVE:      save_delay_preset_as(dir, text);  break;
        case QwertyContext::PROJECT_SAVE_AS:
            // The song takes the name; save_project writes <name>.ptp and makes it the song's file.
            if (host_.project().name != text) {
                host_.edit_project().name = text;
                mark_modified();
            }
            save_project_now();
            break;
        default: break;
    }
}

GestureResult InputDispatcher::qwerty_layer(Gesture g) {
    QwertyKeyboardState& k = s_.qwerty;
    switch (g) {
        case Gesture::DPAD_UP:    move_key_cursor_up(k);    break;
        case Gesture::DPAD_DOWN:  move_key_cursor_down(k);  break;
        case Gesture::DPAD_LEFT:  move_key_cursor_left(k);  break;
        case Gesture::DPAD_RIGHT: move_key_cursor_right(k); break;

        // A types the key — except the layout key and the action row (ABORT col 0, APPLY col 1).
        case Gesture::A:
            if (k.is_on_layout_key())         k.layout = 1 - k.layout;
            else if (!k.is_on_action_row())   insert_current_key(k);
            else if (k.keyCursorCol == 0) qwerty_cancel();
            else                          qwerty_apply();
            break;

        case Gesture::B:      delete_char(k);  break;
        case Gesture::START:  qwerty_apply();  break;
        // B backspaces. SELECT+B abandons the edit, while bare SELECT opens help.
        case Gesture::SELECT_B: qwerty_cancel(); break;

        // R+UP/DOWN switch the layout; R+LEFT/RIGHT move the text cursor.
        case Gesture::R_UP:    k.layout = 0; clamp_col(k); break;
        case Gesture::R_DOWN:  k.layout = 1; clamp_col(k); break;
        case Gesture::R_LEFT:  move_text_cursor_left(k);  break;
        case Gesture::R_RIGHT: move_text_cursor_right(k); break;

        default: break;
    }
    return GestureResult::TAKEN;
}

void InputDispatcher::open_qwerty(QwertyContext context, const std::string& initial_text,
                                  const std::string& field_label, const std::string& context_extra,
                                  int max_length, bool clear_on_first_b) {
    QwertyKeyboardState k{};
    k.isOpen        = true;
    k.text          = initial_text.substr(0, static_cast<size_t>(max_length));
    k.maxLength     = max_length;
    k.textCursor    = static_cast<int>(k.text.size());
    k.fieldLabel    = field_label;
    k.contextExtra  = context_extra;
    k.context       = context;
    k.clearOnFirstB = clear_on_first_b;
    k.insertBefore  = s_.settings.insertBefore;   // read at open, so it cannot change mid-word
    s_.qwerty       = k;
}

void InputDispatcher::qwerty_apply() {
    const QwertyKeyboardState k    = s_.qwerty;   // by value: every arm below closes the keyboard
    const std::string         text = trimmed_text(k);
    s_.qwerty = QwertyKeyboardState{};

    switch (k.context) {
        case QwertyContext::SOURCE_SEARCH:
            source_sampler_submit_query(s_.sourceSampler, text);
            break;
        case QwertyContext::FILE_RENAME: {
            // An empty field means "leave it alone", not "name it nothing".
            const std::string name = text.empty() ? path_stem(k.contextExtra) : text;
            const std::string target = rename_target(fs_, k.contextExtra, name);
            if (fs_.rename_file(k.contextExtra, name)) {
                refresh_browser();
                set_status(s_.fileBrowser, "RENAMED", true);
            } else if (!target.empty() && fs_.file_exists(target)) {
                // Never over another file: the keyboard comes back with what was typed.
                set_status(s_.fileBrowser, "NAME TAKEN", false);
                open_qwerty(QwertyContext::FILE_RENAME, text, k.fieldLabel, k.contextExtra, k.maxLength);
            } else {
                set_status(s_.fileBrowser, "RENAME FAILED", false);
            }
            break;
        }

        case QwertyContext::FOLDER_CREATE: {
            const std::string name = text.empty() ? "NewFolder" : text;
            if (!fs_.create_folder(k.contextExtra, name).empty()) {
                refresh_browser();
                set_status(s_.fileBrowser, "CREATED", true);
            } else {
                set_status(s_.fileBrowser, "CREATE FAILED", false);
            }
            break;
        }

        case QwertyContext::INSTRUMENT_NAME: {
            Instrument& ins = host_.edit_project().instruments[static_cast<size_t>(s_.currentInstrument)];
            // A cleared name reverts to "INSTxx": an unnamed instrument must still be identifiable.
            ins.name = text.empty() ? songcore::default_instrument_name(ins.id) : text;
            mark_modified();
            break;
        }

        case QwertyContext::PROJECT_NAME:
            // An empty name is allowed; SAVE supplies a fallback filename (save_project).
            host_.edit_project().name = text;
            mark_modified();
            break;

        // ⚠️ k.contextExtra, not s_.qwerty's: the live keyboard is already cleared.
        case QwertyContext::INSTRUMENT_SAVE:
        case QwertyContext::THEME_SAVE:
        case QwertyContext::SCALE_SAVE:
        case QwertyContext::GROOVE_SAVE:
        case QwertyContext::REVERB_SAVE:
        case QwertyContext::DELAY_SAVE:
        case QwertyContext::PROJECT_SAVE_AS:
            typed_save(k.context, k.contextExtra, text);
            break;

        case QwertyContext::SAMPLE_NAME: {
            // Renames the editor's sample and its instrument — one thing to the user. Empty keeps the name.
            SampleEditorState& se = s_.sampleEditor;
            const std::string  name = text.empty() ? se.sampleName : text;
            se.sampleName = name;
            host_.edit_project().instruments[static_cast<size_t>(se.instrumentId)].name = name;
            mark_modified();
            break;
        }

        case QwertyContext::SAMPLE_SAVE: {
            // SAVE-AS never replaces (SNARE, SNARE_0001, …): OVERWRITE is its own button.
            const std::string stem = free_wav_stem(fs_, k.contextExtra, text.empty() ? "SAMPLE" : text);
            save_sample_to(k.contextExtra + "/" + stem + ".wav", /*adopt_name=*/true);
            break;
        }

        case QwertyContext::RESAMPLE:
            // Empty → auto Resample_NNNN. The selection is still live.
            resample_selection(text);
            break;
    }
}

}  // namespace pt::ui
