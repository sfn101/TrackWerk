#include "ui/settings_store.h"

#include <algorithm>
#include <string>

#include "ui/sample_granular_presets.h"
#include "ui/sound_browser.h"
#include "ui/theme_io.h"
#include "ui/demo_font_style.h"
#include "vendor/nlohmann/json.hpp"

namespace pt::ui {

namespace {

using nlohmann::json;

// settings.json as load_settings last read it; every save writes this app's values over it.
json& loaded_settings() { static json j = json::object(); return j; }

// ⚠️ Every key serialize_settings writes only sometimes, or turning it off would leave the old value.
const char* const kOptionalKeys[] = {"fontStyle", "sceneMotion", "sourceYoutube", "sampleGranularFavorites",
                                     "soundFavorites", "soundRecent", "soundPreviewLevel", "firstBeatDone",
                                     "wideScreen",
                                     "midi_test_channel"};

// An absent or wrong-typed key leaves the default alone.
bool get_bool(const json& j, const char* key, bool fallback) {
    const auto it = j.find(key);
    if (it == j.end() || !it->is_boolean()) return fallback;
    return it->get<bool>();
}

int get_int(const json& j, const char* key, int fallback) {
    const auto it = j.find(key);
    if (it == j.end() || !it->is_number_integer()) return fallback;
    return it->get<int>();
}

std::string get_string(const json& j, const char* key, const std::string& fallback) {
    const auto it = j.find(key);
    if (it == j.end() || !it->is_string()) return fallback;
    return it->get<std::string>();
}

int clamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

constexpr int VISUALIZER_COUNT = 23;   // VisualizerType — SCOPE … RIDGES

}  // namespace

bool load_settings(FileSystem& fs, SettingsValues& values, Theme& theme) {
    loaded_settings() = json::object();
    std::string blob;
    if (!fs.read_file(fs.settings_path(), blob)) return false;   // first launch

    const json j = json::parse(blob, nullptr, /*allow_exceptions=*/false);
    if (!j.is_object()) return false;
    loaded_settings() = j;

    // SCALING: the three-way "scaling"; an older file's "scalingBilinear" carries over as BILINEAR/INT.
    if (const auto it = j.find("scaling"); it != j.end() && it->is_number_integer() &&
                                           it->get<int64_t>() >= SCALING_INT && it->get<int64_t>() <= SCALING_BILINEAR)
        values.scaling = static_cast<int>(it->get<int64_t>());
    else if (const auto old = j.find("scalingBilinear"); old != j.end() && old->is_boolean())
        values.scaling = old->get<bool>() ? SCALING_BILINEAR : SCALING_INT;
    // Unknown/future IDs and wrong types fall back to the existing classic face.
    values.fontStyle=0;
    if(const auto font=j.find("fontStyle");font!=j.end()){
        if(font->is_number_unsigned()){
            const auto id=font->get<uint64_t>();
            if(id<kDemoFontStyleCount)values.fontStyle=static_cast<int>(id);
        }else if(font->is_number_integer()){
            const auto id=font->get<int64_t>();
            if(id>=0&&id<kDemoFontStyleCount)values.fontStyle=static_cast<int>(id);
        }
    }
    values.sceneMotion        = get_bool(j, "sceneMotion",        true);
    values.sourceYoutube      = get_bool(j, "sourceYoutube",      false);
    values.insertBefore       = get_bool(j, "insertBefore",       values.insertBefore);
    values.cursorRemember     = get_bool(j, "cursorRemember",     values.cursorRemember);
    values.notePreviewEnabled = get_bool(j, "notePreview",        values.notePreviewEnabled);

    // This upgrade enables the requested sample memory once; a later explicit REFRESH persists.
    values.rememberFolder     = get_int(j, "sampleFolderMemoryVersion", 0) >= 1
                                  ? get_bool(j, "rememberFolder", values.rememberFolder) : true;
    values.lastSampleFolder   = get_string(j, "lastSampleFolder", values.lastSampleFolder);
    values.lastSampleSelection = get_string(j, "lastSampleSelection", values.lastSampleSelection);
    values.lastSampleCursor   = clamp(get_int(j, "lastSampleCursor", 0), 0, 1000000);
    values.lastSampleScroll   = clamp(get_int(j, "lastSampleScroll", 0), 0, 1000000);
    values.lastSampleSort     = clamp(get_int(j, "lastSampleSort", 2), 0, FILE_SORT_MODE_COUNT - 1);
    auto& favorites = values.sampleGranularFavorites;
    favorites.clear();
    if (const auto saved = j.find("sampleGranularFavorites"); saved != j.end() && saved->is_array()) {
        for (const auto& id : *saved) {
            if (favorites.size() == kSampleGranularPerCategory) break;
            if (!id.is_number_integer()) continue;
            const auto index64 = id.get<uint64_t>();
            if (index64 >= kSampleGranularFactoryCount) continue;
            const int index = static_cast<int>(index64);
            if (std::find(favorites.begin(), favorites.end(), index) == favorites.end())
                favorites.push_back(index);
        }
    }
    // The sound browser's lists: a damaged or hand-edited entry is skipped, duplicates collapse, and
    // the lists stay bounded; an older file without them leaves both empty.
    auto read_keys = [&](const char* name, std::vector<std::string>& out, int max) {
        out.clear();
        const auto saved = j.find(name);
        if (saved == j.end() || !saved->is_array()) return;
        for (const auto& key : *saved) {
            if (static_cast<int>(out.size()) == max) break;
            if (!key.is_string()) continue;
            const auto k = key.get<std::string>();
            if (sound_key_valid(k) && std::find(out.begin(), out.end(), k) == out.end()) out.push_back(k);
        }
    };
    read_keys("soundFavorites", values.soundFavorites, kSoundFavoritesMax);
    read_keys("soundRecent", values.soundRecent, kSoundRecentMax);
    values.soundPreviewLevel = clamp(get_int(j, "soundPreviewLevel", kSoundPreviewLevelDefault) / 10 * 10,
                                     kSoundPreviewLevelMin, kSoundPreviewLevelMax);
    values.navSongRelative    = get_bool(j, "navSongRelative", values.navSongRelative);
    values.traceEnabled       = get_bool(j, "trace",              values.traceEnabled);

    values.abxyIndex          = clamp(get_int(j, "abxy", values.abxyIndex), 0, 2);
    // TOUCH BUTTONS: the three-way "touchButtons"; an older file's touchButtonsWithPad carries over.
    if (const auto it = j.find("touchButtons"); it != j.end() && it->is_number_integer() &&
                                                it->get<int64_t>() >= TOUCH_AUTO && it->get<int64_t>() <= TOUCH_OFF)
        values.touchButtons = static_cast<int>(it->get<int64_t>());
    else
        values.touchButtons = get_bool(j, "touchButtonsWithPad", false) ? TOUCH_ALWAYS : TOUCH_AUTO;

    values.metronomeEnabled   = get_bool(j, "metronome", values.metronomeEnabled);
    values.metronomeVolume    = clamp(get_int(j, "metronomeVolume", values.metronomeVolume), 0, 255);

    values.helpMode           = clamp(get_int(j, "help", values.helpMode), 0, 2);
    values.interpDefault      = clamp(get_int(j, "interpDefault", values.interpDefault), 0, 4);
    values.interpLimit        = clamp(get_int(j, "interpLimit", values.interpLimit), 0, 4);
    values.keyBar             = get_bool(j, "keyBar", values.keyBar);

    // ── MIDI: the cable's half (the song's half is in the .ptp) ──────────────────────────────────
    // Devices by name — port lists reorder on every replug; a missing name resolves to OFF.
    values.midiOutDevice      = get_string(j, "midi_out_device", values.midiOutDevice);
    values.midiInDevice       = get_string(j, "midi_in_device", values.midiInDevice);
    values.midiOffsetMs       = clamp(get_int(j, "midi_offset_ms", values.midiOffsetMs), -99, 99);
    // ⚠️ After the line above, which its fallback reads: an older file is AUTO only if its offset was 0.
    values.midiOffsetAuto     = get_bool(j, "midi_offset_auto", values.midiOffsetMs == 0);
    values.midiSyncOut        = get_bool(j, "midi_sync_out", values.midiSyncOut);
    values.midiSyncIn         = clamp(get_int(j, "midi_sync_in", values.midiSyncIn), 0, 2);
    // 0-15 or `MIDI_CTL_CH_ALL`; out of range (an older build's `-1`) → ALL.
    values.midiControlChannel = get_int(j, "midi_control_channel", values.midiControlChannel);
    if (values.midiControlChannel < 0 || values.midiControlChannel > songcore::MIDI_CTL_CH_ALL)
        values.midiControlChannel = songcore::MIDI_CTL_CH_ALL;
    values.midiTestChannel = get_int(j, "midi_test_channel", values.midiTestChannel);
    if (values.midiTestChannel < 0 || values.midiTestChannel > 15) values.midiTestChannel = 0;
    values.midiInVoices = get_int(j, "midi_in_voices", values.midiInVoices);
    if (values.midiInVoices < 1 || values.midiInVoices > 8) values.midiInVoices = 4;
    values.midiVelocity = get_bool(j, "midi_velocity", values.midiVelocity);
    values.audioOutput  = get_string(j, "audio_output", values.audioOutput);   // a driver name

    values.autosaveResumeAuto = get_bool(j, "autosaveResumeAuto", values.autosaveResumeAuto);
    values.firstBeatDone = get_bool(j, "firstBeatDone", values.firstBeatDone);
    values.wideScreen = get_bool(j, "wideScreen", values.wideScreen);

    // ── The device rows, read and written on every platform even where not shown ─────────────────
    // ⚠️ Android's prefs import writes these keys before boot; a key one side writes and the other drops
    // would be lost on the first quit. SKIN and OVERLAY are stable names, resolved by the shell.
    values.portraitSkin       = get_string(j, "portrait_skin", values.portraitSkin);
    values.overlayName        = get_string(j, "overlay_name",  values.overlayName);
    values.buttonSoundEnabled = get_bool(j, "buttonSound",  values.buttonSoundEnabled);
    values.buttonVibroEnabled = get_bool(j, "buttonVibro",  values.buttonVibroEnabled);
    values.buttonSoundVolume  = clamp(get_int(j, "buttonSoundVolume", values.buttonSoundVolume), 0, 255);
    values.vibroPower         = clamp(get_int(j, "vibroPower",        values.vibroPower),        0, 255);
    values.overlayStrength    = clamp(get_int(j, "overlayStrength",   values.overlayStrength),   0, 255);

    // The user's, not the theme's: read first, so the theme load below cannot overwrite it.
    const int viz = clamp(get_int(j, "visualizer", static_cast<int>(theme.visualizerType)),
                          0, VISUALIZER_COUNT - 1);

    // ⚠️ The whole palette, not its name: an invented palette exists nowhere else. Through the `.ptt`
    // parser, so the two formats cannot drift.
    if (const auto it = j.find("appTheme"); it != j.end() && it->is_object())
        parse_theme(it->dump(), theme);
    else
        theme = theme_by_name(get_string(j, "theme", theme.name), theme.visualizerType);  // an older file

    theme.visualizerType = static_cast<VisualizerType>(viz);
    return true;
}

namespace {

// The exact bytes settings.json should hold — both the comparison and the write use it.
std::string serialize_settings(const SettingsValues& values, const Theme& theme) {
    // Over the file as loaded, so a key only a newer TrackWerk knows survives this one.
    json j = loaded_settings();
    for (const char* key : kOptionalKeys) j.erase(key);
    j["scaling"]            = values.scaling;
    j["scalingBilinear"]    = values.scaling == SCALING_BILINEAR;   // for older builds
    if(valid_demo_font_style(values.fontStyle))j["fontStyle"]=valid_demo_font_style(values.fontStyle);
    if(!values.sceneMotion)j["sceneMotion"]=false;
    if(values.sourceYoutube)j["sourceYoutube"]=true;
    j["insertBefore"]       = values.insertBefore;
    j["cursorRemember"]     = values.cursorRemember;
    j["notePreview"]        = values.notePreviewEnabled;
    j["rememberFolder"]     = values.rememberFolder;
    j["sampleFolderMemoryVersion"] = 1;
    j["lastSampleFolder"]   = values.lastSampleFolder;
    j["lastSampleSelection"] = values.lastSampleSelection;
    j["lastSampleCursor"]   = values.lastSampleCursor;
    j["lastSampleScroll"]   = values.lastSampleScroll;
    j["lastSampleSort"]     = values.lastSampleSort;
    if (!values.sampleGranularFavorites.empty()) j["sampleGranularFavorites"] = values.sampleGranularFavorites;
    // Written only when used, so an older file stays byte-identical.
    if (!values.soundFavorites.empty()) j["soundFavorites"] = values.soundFavorites;
    if (!values.soundRecent.empty()) j["soundRecent"] = values.soundRecent;
    if (values.soundPreviewLevel != kSoundPreviewLevelDefault) j["soundPreviewLevel"] = values.soundPreviewLevel;
    j["navSongRelative"]    = values.navSongRelative;
    j["abxy"]               = values.abxyIndex;            // 0 AUTO, 1 XBOX, 2 NINTENDO
    j["touchButtons"]       = values.touchButtons;
    j["touchButtonsWithPad"] = values.touchButtons == TOUCH_ALWAYS;   // for older builds
    j["metronome"]          = values.metronomeEnabled;
    j["metronomeVolume"]    = values.metronomeVolume;
    j["help"]               = values.helpMode;             // 0 OFF, 1 SHORT, 2 FULL
    j["interpDefault"]      = values.interpDefault;        // sample-interp.h's InterpMode
    j["interpLimit"]        = values.interpLimit;          // always: the factory value differs per device
    j["keyBar"]             = values.keyBar;
    j["trace"]              = values.traceEnabled;
    j["autosaveResumeAuto"] = values.autosaveResumeAuto;
    if (values.firstBeatDone) j["firstBeatDone"] = true;   // an older file stays byte-identical
    if (values.wideScreen) j["wideScreen"] = true;         // likewise
    j["visualizer"]         = static_cast<int>(theme.visualizerType);

    j["midi_out_device"]    = values.midiOutDevice;
    j["midi_in_device"]     = values.midiInDevice;
    j["midi_offset_ms"]     = values.midiOffsetMs;
    j["midi_offset_auto"]   = values.midiOffsetAuto;
    j["midi_sync_out"]      = values.midiSyncOut;
    j["midi_sync_in"]       = values.midiSyncIn;
    j["midi_control_channel"] = values.midiControlChannel;   // 0-15 (shown 01-16), or MIDI_CTL_CH_ALL
    if (values.midiTestChannel != 0) j["midi_test_channel"] = values.midiTestChannel;   // older files stay as they were
    j["midi_in_voices"]       = values.midiInVoices;         // 1 = MONO, 2-8 = POLY
    j["midi_velocity"]        = values.midiVelocity;
    j["audio_output"]         = values.audioOutput;

    j["portrait_skin"]      = values.portraitSkin;
    j["overlay_name"]       = values.overlayName;
    j["buttonSound"]        = values.buttonSoundEnabled;
    j["buttonSoundVolume"]  = values.buttonSoundVolume;
    j["buttonVibro"]        = values.buttonVibroEnabled;
    j["vibroPower"]         = values.vibroPower;
    j["overlayStrength"]    = values.overlayStrength;

    j["theme"]    = theme.name;   // for older builds
    j["appTheme"] = json::parse(serialize_theme(theme), nullptr, /*allow_exceptions=*/false);

    return j.dump(2) + "\n";
}

}  // namespace

bool save_settings(FileSystem& fs, const SettingsValues& values, const Theme& theme) {
    return fs.write_file(fs.settings_path(), serialize_settings(values, theme));
}

SettingsWrite save_settings_if_changed(FileSystem& fs, const SettingsValues& values,
                                       const Theme& theme) {
    const std::string wanted = serialize_settings(values, theme);

    // A file that cannot be read is written.
    std::string current;
    if (fs.read_file(fs.settings_path(), current) && current == wanted) return SettingsWrite::UNCHANGED;

    return save_settings(fs, values, theme) ? SettingsWrite::SAVED : SettingsWrite::FAILED;
}

SettingsWrite SettingsAutosave::poll(FileSystem& fs, const SettingsValues& values,
                                     const Theme& theme, uint64_t nowMs, bool deferWrite) {
    if (nowMs < nextCheckMs_) return SettingsWrite::UNCHANGED;
    nextCheckMs_ = nowMs + kCheckEveryMs;

    std::string wanted = serialize_settings(values, theme);
    if (!primed_) {
        // The file as loaded at start-up; a missing or unreadable one is written once it settles.
        if (!fs.read_file(fs.settings_path(), onDisk_)) onDisk_.clear();
        seen_ = wanted;
        seenAtMs_ = nowMs;
        primed_ = true;
    } else if (wanted != seen_) {
        seen_ = std::move(wanted);
        seenAtMs_ = nowMs;
    }
    if (seen_ == onDisk_ || deferWrite || nowMs - seenAtMs_ < kQuietForMs)
        return SettingsWrite::UNCHANGED;

    if (!fs.write_file(fs.settings_path(), seen_)) {
        seenAtMs_ = nowMs;   // back off one quiet period instead of retrying every second
        return SettingsWrite::FAILED;
    }
    onDisk_ = seen_;
    return SettingsWrite::SAVED;
}

SettingsWrite SettingsAutosave::flush(FileSystem& fs, const SettingsValues& values, const Theme& theme) {
    const std::string wanted = serialize_settings(values, theme);
    std::string current;
    const bool readable = fs.read_file(fs.settings_path(), current);
    primed_ = true;
    if (readable && current == wanted) {
        onDisk_ = std::move(current);
        return SettingsWrite::UNCHANGED;
    }
    if (!fs.write_file(fs.settings_path(), wanted)) {
        // Not confirmed: the cache keeps what the file held before (or nothing), so the next poll
        // still sees a difference and tries again after its quiet period.
        if (readable) onDisk_ = std::move(current); else onDisk_.clear();
        return SettingsWrite::FAILED;
    }
    onDisk_ = wanted;
    return SettingsWrite::SAVED;
}

}  // namespace pt::ui
