#pragma once

// `.ptt`, a theme as a file. Parsed tolerantly; written byte-exact (a golden pins it):
//   * a field equal to `Theme{}`'s (CLASSIC) is omitted, so an unmodified CLASSIC is `{}`;
//   * 4-space indent, keys in declaration order, no trailing newline;
//   * a colour is a plain decimal (0xFF0A0A0A is `4278518794`);
//   * `visualizerType` is its serial name — not the settings label, which would read back as SCOPE.

#include <cstdint>
#include <string>

#include "songcore/project_io.h"   // JsonWriter + JsonLayout
#include "ui/filesystem.h"
#include "ui/theme.h"
#include "ui/scene_presets.h"
#include "ui/demo_font_style.h"
#include "vendor/nlohmann/json.hpp"

namespace pt::ui {

inline const char* visualizer_serial_name(VisualizerType v) {
    switch (v) {
        case VisualizerType::SCOPE:          return "SCOPE";
        case VisualizerType::FLAT:           return "FLAT";
        case VisualizerType::OCTA:           return "OCTA";
        case VisualizerType::OCTA_FULL:      return "OCTA_FULL";
        case VisualizerType::SPECTRUM:       return "SPECTRUM";
        case VisualizerType::SPECTRUM_PEAKS: return "SPECTRUM_PEAKS";
        case VisualizerType::COPPER:         return "COPPER";
        case VisualizerType::STARFIELD:      return "STARFIELD";
        case VisualizerType::PLASMA:         return "PLASMA";
        case VisualizerType::TUNNEL:        return "TUNNEL";
        case VisualizerType::FIRE:          return "FIRE";
        case VisualizerType::RAIN:          return "RAIN";
        case VisualizerType::TWISTER:       return "TWISTER";
        case VisualizerType::RINGS:         return "RINGS";
        case VisualizerType::KALEIDO:       return "KALEIDO";
        case VisualizerType::BLOBS:         return "BLOBS";
        case VisualizerType::ROTOZOOM:      return "ROTOZOOM";
        case VisualizerType::CUBE:          return "CUBE";
        case VisualizerType::GLOBE:         return "GLOBE";
        case VisualizerType::TERRAIN:       return "TERRAIN";
        case VisualizerType::TORUS:         return "TORUS";
        case VisualizerType::DOTBALL:       return "DOTBALL";
        case VisualizerType::RIDGES:        return "RIDGES";
    }
    return "SCOPE";
}

// An unknown name reads as the default.
inline VisualizerType visualizer_from_serial_name(const std::string& s) {
    if (s == "FLAT")           return VisualizerType::FLAT;
    if (s == "OCTA")           return VisualizerType::OCTA;
    if (s == "OCTA_FULL")      return VisualizerType::OCTA_FULL;
    if (s == "SPECTRUM")       return VisualizerType::SPECTRUM;
    if (s == "SPECTRUM_PEAKS") return VisualizerType::SPECTRUM_PEAKS;
    if (s == "COPPER")         return VisualizerType::COPPER;
    if (s == "STARFIELD")      return VisualizerType::STARFIELD;
    if (s == "PLASMA")         return VisualizerType::PLASMA;
    if (s == "TUNNEL")         return VisualizerType::TUNNEL;
    if (s == "FIRE")           return VisualizerType::FIRE;
    if (s == "RAIN")           return VisualizerType::RAIN;
    if (s == "TWISTER")        return VisualizerType::TWISTER;
    if (s == "RINGS")          return VisualizerType::RINGS;
    if (s == "KALEIDO")        return VisualizerType::KALEIDO;
    if (s == "BLOBS")          return VisualizerType::BLOBS;
    if (s == "ROTOZOOM")       return VisualizerType::ROTOZOOM;
    if (s == "CUBE")           return VisualizerType::CUBE;
    if (s == "GLOBE")          return VisualizerType::GLOBE;
    if (s == "TERRAIN")        return VisualizerType::TERRAIN;
    if (s == "TORUS")          return VisualizerType::TORUS;
    if (s == "DOTBALL")        return VisualizerType::DOTBALL;
    if (s == "RIDGES")         return VisualizerType::RIDGES;
    return VisualizerType::SCOPE;   // incl. "SCOPE", and any value another build might write
}

inline std::string serialize_theme(const Theme& t) {
    const Theme d{};   // the field defaults — the omission yardstick
    songcore::JsonWriter w{songcore::JsonLayout::Pretty};
    w.begin_object();

    if (t.name != d.name) w.field_string("name", t.name);

    // ⚠️ Spelled out, not from `theme_color_rows()`: this order is the file format.
    auto color = [&](const char* key, Argb v, Argb dv) {
        if (v != dv) w.field_int(key, static_cast<long long>(v));
    };

    color("background",      t.background,      d.background);
    color("rowEvery4th",     t.rowEvery4th,     d.rowEvery4th);
    color("rowCursor",       t.rowCursor,       d.rowCursor);
    color("rowPlayback",     t.rowPlayback,     d.rowPlayback);
    color("rowSelection",    t.rowSelection,    d.rowSelection);
    color("textTitle",       t.textTitle,       d.textTitle);
    color("textParam",       t.textParam,       d.textParam);
    color("textValue",       t.textValue,       d.textValue);
    color("textCursor",      t.textCursor,      d.textCursor);
    color("textEmpty",       t.textEmpty,       d.textEmpty);
    color("vizBackground",   t.vizBackground,   d.vizBackground);
    color("vizCenterLine",   t.vizCenterLine,   d.vizCenterLine);
    color("vizWave",         t.vizWave,         d.vizWave);
    color("meterBackground", t.meterBackground, d.meterBackground);
    color("meterLow",        t.meterLow,        d.meterLow);
    color("meterMid",        t.meterMid,        d.meterMid);
    color("meterHigh",       t.meterHigh,       d.meterHigh);
    color("meterBorder",     t.meterBorder,     d.meterBorder);   // no editor row; still a field

    // The six borrowed keys are omitted when they equal what `derive_borrowed_colors` gives this theme.
    Theme e = t;
    derive_borrowed_colors(e);
    color("eqBg",     t.eqBg,     e.eqBg);
    color("eqFill",   t.eqFill,   e.eqFill);
    color("eqBorder", t.eqBorder, e.eqBorder);
    color("eqTxt",    t.eqTxt,    e.eqTxt);

    color("textSelection", t.textSelection, e.textSelection);
    color("textPlayhead",  t.textPlayhead,  e.textPlayhead);

    if (t.visualizerType != d.visualizerType)
        w.field_string("visualizerType", visualizer_serial_name(t.visualizerType));
    if (t.surfaceStyle != d.surfaceStyle) w.field_int("surfaceStyle", t.surfaceStyle);
    if (t.chainGrid != d.chainGrid) w.field_string("chainGrid", step_grid_style_name(t.chainGrid));
    if (t.phraseGrid != d.phraseGrid) w.field_string("phraseGrid", step_grid_style_name(t.phraseGrid));

    if (t.sceneChrome != d.sceneChrome) {
        w.field_int("sceneChrome", t.sceneChrome);
        w.field_int("sceneTypographyVersion", 2);
    }
    if (t.fontOverride != d.fontOverride) w.field_int("fontOverride", t.fontOverride);

    w.end_object();
    return std::move(w.out);
}

// Tolerant: unknown keys are ignored, a bad key keeps the default. False only when it is not a JSON object.
inline bool parse_theme(const std::string& text, Theme& out) {
    const nlohmann::json j = nlohmann::json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (!j.is_object()) return false;

    Theme t{};   // the field defaults fill every key the file lacks

    if (const auto it = j.find("name"); it != j.end() && it->is_string())
        t.name = it->get<std::string>();

    auto color = [&](const char* key, Argb& field) {
        const auto it = j.find(key);
        if (it == j.end() || !it->is_number_integer()) return;
        field = static_cast<Argb>(static_cast<uint64_t>(it->get<int64_t>()) & 0xFFFFFFFFu);
    };

    color("background",      t.background);
    color("rowEvery4th",     t.rowEvery4th);
    color("rowCursor",       t.rowCursor);
    color("rowPlayback",     t.rowPlayback);
    color("rowSelection",    t.rowSelection);
    color("textTitle",       t.textTitle);
    color("textParam",       t.textParam);
    color("textValue",       t.textValue);
    color("textCursor",      t.textCursor);
    color("textEmpty",       t.textEmpty);
    color("vizBackground",   t.vizBackground);
    color("vizCenterLine",   t.vizCenterLine);
    color("vizWave",         t.vizWave);
    color("meterBackground", t.meterBackground);
    color("meterLow",        t.meterLow);
    color("meterMid",        t.meterMid);
    color("meterHigh",       t.meterHigh);
    color("meterBorder",     t.meterBorder);

    // Derived from what was read, then overwritten by whatever the file names.
    derive_borrowed_colors(t);
    color("eqBg",     t.eqBg);
    color("eqFill",   t.eqFill);
    color("eqBorder", t.eqBorder);
    color("eqTxt",    t.eqTxt);
    color("textSelection", t.textSelection);
    color("textPlayhead",  t.textPlayhead);

    if (const auto it = j.find("visualizerType"); it != j.end() && it->is_string())
        t.visualizerType = visualizer_from_serial_name(it->get<std::string>());
    if (const auto it=j.find("surfaceStyle");it!=j.end()&&it->is_number_integer()) {
        const auto value=it->get<int64_t>();
        if(value>=0&&value<kSurfaceStyleCount)t.surfaceStyle=static_cast<int>(value);
    }
    const auto read_grid = [&](const char* key, StepGridStyle& field) {
        const auto it = j.find(key);
        if (it == j.end() || !it->is_string()) return;
        const auto& value = it->get_ref<const std::string&>();
        for (int index = 0; index < STEP_GRID_STYLE_COUNT; ++index) {
            const auto style = static_cast<StepGridStyle>(index);
            if (value == step_grid_style_name(style)) {
                field = style;
                return;
            }
        }
    };
    read_grid("chainGrid", t.chainGrid);
    read_grid("phraseGrid", t.phraseGrid);

    if (const auto it = j.find("sceneChrome"); it != j.end() && it->is_number_integer()) {
        const auto value = it->get<uint64_t>();
        if (value <= kSceneCount) t.sceneChrome = static_cast<int>(value);
    }
    if (const auto it = j.find("fontOverride"); it != j.end() && it->is_number_integer()) {
        const auto value = it->get<uint64_t>();
        if (value < kDemoFontStyleCount) t.fontOverride = static_cast<int>(value);
    }

    if (t.sceneChrome > 0 && !j.contains("sceneTypographyVersion")) {
        const int scene = t.sceneChrome - 1;
        if (t.fontOverride == kOriginalSceneFonts[scene]) t.fontOverride = kScenePresets[scene].font;
    }

    out = t;
    return true;
}

inline bool save_theme_file(FileSystem& fs, const std::string& path, const Theme& theme) {
    return fs.write_file(path, serialize_theme(theme));
}

// The visualizer is not taken from the file: it is the user's, not the theme's.
inline bool load_theme_file(FileSystem& fs, const std::string& path, Theme& out) {
    std::string text;
    if (!fs.read_file(path, text)) return false;

    Theme loaded{};
    if (!parse_theme(text, loaded)) return false;

    loaded.visualizerType = out.visualizerType;   // the user's, kept across the load
    out = loaded;
    return true;
}

}  // namespace pt::ui
