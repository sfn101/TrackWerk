#include "ui/demo_fx.h"
#include "ui/perform.h"
#include "ui/layout.h"
#include "ui/studio.h"
#include "ui/scene_picker.h"
#include "ui/scene_layout.h"
#include "ui/scene_animation.h"
#include "ui/live_resample.h"
#include "ui/extreme_splash.h"
#include "ui/brand_graphics.h"

#include <algorithm>
#include <string>

#include "songcore/chords.h"
#include "songcore/scales.h"
#include "ui/clipboard.h"
#include "ui/helpers.h"
#include "ui/modules/fx_helper_overlay.h"
#include "ui/modules/map_picker_overlay.h"
#include "ui/modules/render_dialog.h"
#include "ui/song_pointer.h"

namespace pt::ui {

namespace {

constexpr int STATUS_MAX_CHARS = 34;   // one line

// Is an overlay drawn in the editor's place? ⚠️ Mirrors `draw_editor`'s ifs: a new overlay there goes
// here too, or `has_falling_meters` holds the redraw open for ever.
bool editor_overlay_up(const AppState& s) {
    return s.eq.isOpen || s.themeEditor.isOpen;
}

bool phrase_overview_visible(const AppState& s) {
    return s.project && s.currentScreen == ScreenType::PHRASE && !editor_overlay_up(s) &&
           s.phraseOverview != PhraseOverview::AUDIO && s.currentPhrase >= 0 &&
           static_cast<size_t>(s.currentPhrase) < s.project->phrases.size();
}
}  // namespace

void TrackerLayout::draw(Canvas& c, const AppState& s) {
    c.set_appearance(s.theme, s.settings.fontStyle);
    c.clear_cursor_mark();
    c.set_clock(s.animationMs, s.settings.sceneMotion,
                s.scenePicker >= 0 ? 3000 : s.studioOpen ? 1000 + s.studioTab : static_cast<int>(s.currentScreen));
    // Motion switched off (or no clock): effects already under way end at once, not when they run out.
    if (s.animationMs == 0 || !s.settings.sceneMotion) {
        statusUntil_ = railUntil_ = selectorUntil_ = 0;
        std::fill(std::begin(railStart_), std::end(railStart_), uint64_t{0});
    }
    if(s.splashProgress>=0){draw_extreme_splash(c,s.theme,s.splashProgress);return;}
    if(s.attractActive){demo_fx::draw_attract(c,s,s.animationMs-s.attractStarted);return;}
    if(s.showActive){demo_fx::draw_show(c,s,&showPeaks_);return;}
    if(s.localSampleRange.isOpen){draw_local_sample_range(c,s.localSampleRange,s.theme);draw_loading_strip(c,s.loading,s.theme);return;}
    if (s.scenePicker >= 0) {
        draw_scene_picker(c, s.scenePicker, s.theme, s.settings.sceneMotion ? s.animationMs : 0);
    } else if (s.studioOpen) {
        draw_studio(c, s);
        if (s.qwerty.isOpen) qwerty_.draw(c, s.qwerty, s.theme);
    } else {
        draw_frame(c, s);
    }
    apply_glide(c, s);
    apply_selector_glide(c, s);
    draw_first_beat(c, s);   // the tutorial card, under help

    if (s.project) {
        if (s.helpFull) helpOverlay_.draw(c, help_topic(s), s.theme);
        else if (s.helpOpen) helpPanel_.draw(c, help_topic(s), s.theme);
        if (s.helpFull || s.helpOpen) draw_first_beat_help_strip(c, s);   // START: First beat
    }
    draw_loading_strip(c, s.loading, s.theme);
    draw_live_resample_indicator(c,s);
    // Last: it is reachable from Studio too, which does not draw the editor's overlays.
    draw_audio_drivers(c, s.audioDrivers, s.theme);
}

// The cursor's highlight glides from its last cell to the new one: 110 ms, ease-out, a translucent bar
// with a bright edge over the content, while the real cursor already shows the destination. Nothing
// without a running clock (tests, stills), with Motion off, on a page change (the blinds cover that)
// or under a help panel.
void TrackerLayout::apply_glide(Canvas& c, const AppState& s) {
    constexpr uint64_t kGlideMs = 110;
    const auto mark = c.cursor_mark();
    const uint64_t now = s.animationMs;
    const int page = s.scenePicker >= 0 ? 3000 : s.studioOpen ? 1000 + s.studioTab : static_cast<int>(s.currentScreen);
    if (now == 0 || !s.settings.sceneMotion || mark.w <= 0 || s.helpOpen || s.helpFull) {
        glideHave_ = false;
        glideUntil_ = flashUntil_ = 0;
        flashVersion_ = s.projectVersion;
        return;
    }
    // An edit landed (the project changed) while the cursor stayed: its cell flashes for 150 ms.
    constexpr uint64_t kFlashMs = 150;
    if (flashVersion_ >= 0 && s.projectVersion != flashVersion_ && glideHave_ && page == glidePage_) {
        flashStart_ = now;
        flashUntil_ = now + kFlashMs;
    }
    flashVersion_ = s.projectVersion;
    if (flashUntil_ > now) {
        const float p = float(now - flashStart_) / float(kFlashMs);
        c.fill_rect(mark.x, mark.y, mark.w, mark.h, with_alpha(s.theme.textValue, .38f * (1.f - p) * (1.f - p)));
    }
    const auto same = [](const Canvas::Rect& a, const Canvas::Rect& b) {
        return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
    };
    const auto at = [&](float t) {
        const auto lerp = [t](int a, int b) { return a + static_cast<int>(std::lround((b - a) * t)); };
        return Canvas::Rect{lerp(glideFrom_.x, glideTarget_.x), lerp(glideFrom_.y, glideTarget_.y),
                            lerp(glideFrom_.w, glideTarget_.w), lerp(glideFrom_.h, glideTarget_.h)};
    };
    const auto eased = [&](uint64_t time) {
        const float p = std::min(1.f, float(time - glideStart_) / float(kGlideMs));
        return 1.f - (1.f - p) * (1.f - p) * (1.f - p);
    };
    if (!glideHave_ || page != glidePage_) {
        glideHave_ = true;
        glidePage_ = page;
        glideTarget_ = glideFrom_ = mark;
        glideUntil_ = 0;
        return;
    }
    if (!same(mark, glideTarget_)) {
        glideFrom_ = glideUntil_ > now ? at(eased(now)) : glideTarget_;   // from wherever it is now
        glideTarget_ = mark;
        glideStart_ = now;
        glideUntil_ = now + kGlideMs;
    }
    if (glideUntil_ <= now) return;
    const float e = eased(now);
    const auto r = at(e);
    if (same(r, glideTarget_)) return;
    const Argb ink = s.theme.rowCursor;
    const float fade = 1.f - e * .7f;
    c.fill_rect(r.x, r.y, r.w, r.h, with_alpha(ink, .30f * fade));
    c.fill_rect(r.x, r.y, r.w, 1, with_alpha(ink, .85f * fade));
    c.fill_rect(r.x, r.y + r.h - 1, r.w, 1, with_alpha(ink, .85f * fade));
}

// A selector's highlight (Studio tabs, the screen map) glides 140 ms, ease-out, from the entry it
// left to the new one, while the page behind it changes. Like the cursor glide: a translucent bar
// over the content, the real highlight already at the destination; nothing without a clock, with
// Motion off, or between two different selectors.
void TrackerLayout::apply_selector_glide(Canvas& c, const AppState& s) {
    constexpr uint64_t kSelectorMs = 140;
    const auto mark = c.selector_mark();
    const uint64_t now = s.animationMs;
    if (now == 0 || !s.settings.sceneMotion || mark.id < 0 || mark.at.w <= 0 || mark.id != selectorId_) {
        selectorId_ = now == 0 || !s.settings.sceneMotion ? -1 : mark.id;
        selectorFrom_ = selectorTarget_ = mark.at;
        selectorUntil_ = 0;
        return;
    }
    const auto same = [](const Canvas::Rect& a, const Canvas::Rect& b) {
        return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
    };
    const auto at = [&](float t) {
        const auto lerp = [t](int a, int b) { return a + static_cast<int>(std::lround((b - a) * t)); };
        return Canvas::Rect{lerp(selectorFrom_.x, selectorTarget_.x), lerp(selectorFrom_.y, selectorTarget_.y),
                            lerp(selectorFrom_.w, selectorTarget_.w), lerp(selectorFrom_.h, selectorTarget_.h)};
    };
    const auto eased = [&](uint64_t time) {
        const float p = std::min(1.f, float(time - selectorStart_) / float(kSelectorMs));
        return 1.f - (1.f - p) * (1.f - p) * (1.f - p);
    };
    if (!same(mark.at, selectorTarget_)) {
        selectorFrom_ = selectorUntil_ > now ? at(eased(now)) : selectorTarget_;   // from wherever it is now
        selectorTarget_ = mark.at;
        selectorStart_ = now;
        selectorUntil_ = now + kSelectorMs;
    }
    if (selectorUntil_ <= now) return;
    const float e = eased(now);
    const auto r = at(e);
    if (same(r, selectorTarget_)) return;
    const Argb ink = s.theme.rowCursor;
    const float fade = 1.f - e * .6f;
    c.fill_rect(r.x, r.y, r.w, r.h, with_alpha(ink, .32f * fade));
    c.fill_rect(r.x, r.y + r.h - 2, r.w, 2, with_alpha(ink, .9f * fade));
}

void TrackerLayout::draw_frame(Canvas& c, const AppState& s) {
    c.surface_rect(0, 0, DESIGN_W, DESIGN_H, s.theme.background);

    if (!s.project) return;

    if (full_screen_module(s)) {
        draw_full_screen(c, s);
        return;
    }

    const bool settings = s.currentScreen == ScreenType::SETTINGS;
    const auto scene = settings ? SceneLayout{} : scene_layout(s.theme);
    constexpr int editorWidth = EDITOR_CLIP_RIGHT - SIDE_SPACER;
    const int editorX = scene.railLeft ? DESIGN_W - SIDE_SPACER - editorWidth : SIDE_SPACER;
    const int editorY = scene.scopeBottom ? SCREEN_SPACER : EDITOR_Y;
    const int scopeY = scene.scopeBottom ? DESIGN_H - SCREEN_SPACER - OscilloscopeModule::HEIGHT
                                         : SCREEN_SPACER;

    draw_scope_strip(c, s, scopeY);
    draw_editor(c, s, editorX, editorY);

    if (s.currentScreen != ScreenType::VISUALIZER) {   // VI has no title row to underline
        c.fill_rect(editorX, editorY + ROW_HEIGHT + 4, editorWidth, 1,
                    with_alpha(s.theme.textParam, .22f));
        c.fill_rect(editorX, editorY + ROW_HEIGHT + 4, 36, 1, s.theme.rowCursor);
    }
    c.stroke_rect(SIDE_SPACER, scopeY, OscilloscopeModule::WIDTH,
                  OscilloscopeModule::HEIGHT, with_alpha(s.theme.textParam, .18f));

    if (s.theme.sceneChrome) {
        const Argb frame = with_alpha(s.theme.textTitle, .44f);
        c.scene_edge(SIDE_SPACER, scopeY, OscilloscopeModule::WIDTH, OscilloscopeModule::HEIGHT, frame);
        c.scene_edge(editorX - 3, editorY - 2, editorWidth + 6, DESIGN_H - EDITOR_Y - 4, frame);
    }
    if (!settings) draw_track_rail(c, s, scene.railLeft ? SIDE_SPACER : RIGHT_BAR_X,
                                  editorY, scene.railLeft);

    draw_status_line(c, s, scopeY);
    draw_selection_clipboard(c, s, scopeY);
    // The editor is the page; title, scope and track rail stay while it changes (shell/app.cpp).
    c.mark_stage(editorX - 3, editorY - 2, editorWidth + 6, DESIGN_H - EDITOR_Y - 4);

    draw_overlays(c, s);
}

// ─── The file browser and sample editor: full screen, no furniture ───────────────────────────────
void TrackerLayout::draw_full_screen(Canvas& c, const AppState& s) {
    const Theme& t = s.theme;
    if (s.currentScreen == ScreenType::FILE_BROWSER) {
        fileBrowser_.draw(c, 0, 0, s.fileBrowser, t);
    } else {
        sampleEditor_.draw(c, 0, 0, s.sampleEditor, t);
        if (s.project) draw_stretch_editor(c, s.sampleEditor, s.project->tempo, t);
    }
    if (s.qwerty.isOpen) qwerty_.draw(c, s.qwerty, t);
    // A question asked from the browser (REPAIR_SONG, M8_IMPORT) is drawn over it, or it would be
    // answered blind.
    draw_confirm_dialog(c, s.confirm, t);
}

// ─── The oscilloscope or Phrase overview ──────────────────────────────────────────────────────────
void TrackerLayout::draw_scope_strip(Canvas& c, const AppState& s, int y) {
    const Theme& t = s.theme;

    if (phrase_overview_visible(s)) {
        PhraseOverviewState view{s.project->phrases[static_cast<size_t>(s.currentPhrase)],
                                 s.playheads, t, s.phraseOverview, s.cursorRow};
        if (s.selection.active) {
            const auto bounds = s.selection.bounds();
            view.selectedFirst = bounds.topLeftRow;
            view.selectedLast = bounds.bottomRightRow;
        }
        phraseEditor_.draw_overview(c, SIDE_SPACER, y, view);
        return;
    }

    oscilloscope_.draw(c, SIDE_SPACER, y, scope_state(s));
}

// What the visualizer is handed, the strip's and VI's alike.
OscilloscopeState TrackerLayout::scope_state(const AppState& s) {
    const Theme& t = s.theme;
    OscilloscopeState os;
    os.waveform = s.waveform;
    os.theme    = t;
    os.playing     = s.isPlaying && s.settings.sceneMotion;   // Appearance > Motion
    os.animationMs = s.animationMs;
    os.bpm         = s.project ? songcore::tempo_in_force(*s.project) : 120;

    const bool isOctaFull = (t.visualizerType == VisualizerType::OCTA_FULL);
    const bool isOcta     = (t.visualizerType == VisualizerType::OCTA);
    if (isOcta || isOctaFull || t.visualizerType == VisualizerType::COPPER) {
        os.trackWaveforms = s.trackWaveforms;
        if (t.visualizerType == VisualizerType::COPPER) os.activeTrackMask = s.trackMask & 0xFF;
    }

    // OCTA_FULL shows all eight lanes; OCTA the tracks that have played, plus the preview lane when stopped.
    if (isOctaFull) {
        os.activeTrackMask = 0xFF;
    } else if (isOcta) {
        os.activeTrackMask = s.trackMask & 0xFF;
        if (!s.isPlaying && s.previewLaneActive) os.activeTrackMask |= (1 << PREVIEW_LANE);
    }

    if (t.visualizerType == VisualizerType::SPECTRUM ||
        t.visualizerType == VisualizerType::SPECTRUM_PEAKS) {
        os.spectrum = s.spectrum;
    }
    return os;
}

// VI: the visualizer across the main area, redrawn for its size; the style's name fades in after a change.
void TrackerLayout::draw_visualizer_screen(Canvas& c, const AppState& s, int x, int y) {
    constexpr int w = EDITOR_CLIP_RIGHT - SIDE_SPACER;
    constexpr int h = DESIGN_H - EDITOR_Y - SCREEN_SPACER;
    visualizer_.draw_sized(c, x, y, w, h, scope_state(s));
    if (s.vizNameUntilMs <= s.animationMs) return;
    const float a = std::min(1.f, float(s.vizNameUntilMs - s.animationMs) / 250.f);   // fades over the last 250 ms
    const auto& names = SettingsModule::visualizer_names();
    const int i = std::clamp(static_cast<int>(s.theme.visualizerType), 0, static_cast<int>(names.size()) - 1);
    const std::string& name = names[static_cast<size_t>(i)];
    const int tw = Canvas::text_width(name, 2, 3);
    c.fill_rect(x + (w - tw) / 2 - 10, y + 12, tw + 20, 27, with_alpha(s.theme.background, .85f * a));
    c.draw_text(name, x + (w - tw) / 2, y + 18, with_alpha(s.theme.textTitle, a), 2, 3);
}

// ─── What each screen's module is handed ─────────────────────────────────────────────────────────

namespace {

const songcore::Instrument& current_instrument(const AppState& s) {
    return s.project->instruments[static_cast<size_t>(s.currentInstrument)];
}

PhraseEditorState phrase_state(const AppState& s) {
    const songcore::Project& p = *s.project;
    PhraseEditorState ps{p.phrases[static_cast<size_t>(s.currentPhrase)]};
    ps.cursorRow      = s.cursorRow;
    ps.cursorColumn   = s.cursorColumn;
    std::copy(std::begin(s.playheads), std::end(s.playheads), std::begin(ps.playheads));
    ps.selectionMode  = s.selection_mode();
    ps.isCellSelected = [&s](int row, int col) { return s.is_cell_selected(row, col); };
    ps.theme          = s.theme;
    ps.project        = &p;   // whether an AUS/AUF cell is live depends on every walk
    // Under NAV = SONG, the chain row and song cell the phrase is seen through; −1 under POOL.
    if (s.settings.navSongRelative) {
        ps.viaChain    = s.currentChain;
        ps.viaChainRow = pointer_chain_row(s);
        ps.songRow     = pointer_song_row(s);
        ps.songTrack   = pointer_track(s);
    }
    return ps;
}

ChainEditorState chain_state(const AppState& s) {
    ChainEditorState cs{s.project->chains[static_cast<size_t>(s.currentChain)]};
    cs.cursorRow      = s.cursorRow;
    cs.cursorColumn   = s.cursorColumn;
    std::copy(std::begin(s.playheads), std::end(s.playheads), std::begin(cs.playheads));
    cs.selectionMode  = s.selection_mode();
    cs.isCellSelected = [&s](int row, int col) { return s.is_cell_selected(row, col); };
    cs.theme          = s.theme;
    if (s.settings.navSongRelative) {
        cs.songRow   = pointer_song_row(s);
        cs.songTrack = pointer_track(s);
    }
    return cs;
}

SongEditorState song_state(const AppState& s) {
    SongEditorState ss{*s.project};
    ss.cursorRow      = s.cursorRow;
    ss.cursorTrack    = s.cursorColumn;
    ss.scrollPosition = s.songScrollPosition;
    std::copy(std::begin(s.playheads), std::end(s.playheads), std::begin(ss.playheads));
    ss.liveMode       = s.liveMode;
    std::copy(std::begin(s.liveQueue), std::end(s.liveQueue), std::begin(ss.liveQueue));
    ss.blinkPhaseMs   = s.blinkPhaseMs;
    ss.selectionMode  = s.selection_mode();
    ss.isCellSelected = [&s](int row, int col) { return s.is_cell_selected(row, col); };
    ss.theme          = s.theme;
    return ss;
}

TableState table_state(const AppState& s) {
    TableState ts{s.project->tables[static_cast<size_t>(s.currentTable)]};
    ts.cursorRow    = s.tableCursorRow;
    ts.cursorColumn = s.tableCursorColumn;
    for (int l = 0; l < TABLE_LANES; ++l) ts.playbackRows[l] = s.tablePlaybackRows[l];
    // The instrument's tic rate: one table can run at two speeds under two instruments.
    ts.ticRate        = current_instrument(s).tableTicRate;
    ts.selectionMode  = s.selection_mode();
    ts.isCellSelected = [&s](int row, int col) { return s.is_cell_selected(row, col); };
    ts.theme          = s.theme;
    return ts;
}

GrooveState groove_state(const AppState& s) {
    GrooveState gs{s.project->grooves[static_cast<size_t>(s.currentGroove)]};
    gs.cursorRow    = s.grooveCursorRow;
    gs.cursorColumn = s.grooveCursorColumn;
    gs.panelRow     = s.groovePanelRow;
    gs.panelColumn  = s.groovePanelColumn;
    gs.quantize     = s.grooveQuantize;
    gs.theme        = s.theme;
    return gs;
}

ScaleState scale_state(const AppState& s) {
    ScaleState cs{s.project->scales[static_cast<size_t>(s.currentScale)]};
    cs.key          = s.project->scaleKey;
    cs.cursorRow    = s.scaleCursorRow;
    cs.cursorColumn = s.scaleCursorColumn;
    // The pitch classes sounding now, from the voices (the sequencer runs ahead), all tracks in one mask.
    for (int i = 0; i < 8; ++i) {
        const songcore::Note n = s.trackNotes[i];
        if (n == songcore::Note::EMPTY()) continue;
        const int midi = songcore::note_to_midi(n);
        if (midi >= 0) cs.soundingMask |= 1u << songcore::scale_mod12(midi);
    }
    cs.theme = s.theme;
    return cs;
}

InstrumentEditorState instrument_state(const AppState& s) {
    InstrumentEditorState is{current_instrument(s)};
    is.cursorRow     = s.instrumentCursorRow;
    is.cursorColumn  = s.instrumentCursorColumn;
    is.sfPresetName  = s.sfPresetName;
    is.sfPresetCount = s.sfPresetCount;
    is.sfPresetIndex = s.sfPresetIndex;
    is.theme         = s.theme;
    return is;
}

InstrumentPoolState pool_state(const AppState& s) {
    InstrumentPoolState ps{*s.project};
    ps.selectedInstrument = s.currentInstrument;   // its cursor ROW is the selected instrument itself
    ps.cursorColumn       = s.poolCursorColumn;
    ps.sampleRamBytes     = s.sampleRamBytes;
    ps.caps               = s.caps;
    ps.theme              = s.theme;
    return ps;
}

ModulationState mods_state(const AppState& s) {
    ModulationState ms{current_instrument(s)};
    ms.cursorRow  = s.modCursorRow;
    ms.cursorPair = s.modCursorPair;
    ms.cursorSide = s.modCursorSide;
    ms.theme      = s.theme;
    return ms;
}

MixerState mixer_state(const AppState& s) {
    MixerState xs{*s.project};
    xs.cursorColumn   = s.mixerCursorColumn;
    xs.mixerMasterRow = s.mixerMasterRow;
    xs.trackPeaks     = s.trackPeaks;
    xs.masterPeaks    = s.masterPeaks;
    xs.masterMaximum  = s.masterMaximum;
    xs.limiterInputMaximum = s.limiterInputMaximum;
    xs.reverbPeaks    = &s.sendPeaks[0];
    xs.delayPeaks     = &s.sendPeaks[2];
    xs.peaksVersion   = s.peaksVersion;
    xs.theme          = s.theme;
    return xs;
}

EffectState effects_state(const AppState& s) {
    EffectState es{*s.project};
    es.cursorRow = s.effectsCursorRow;
    es.theme     = s.theme;
    return es;
}

ProjectState project_state(const AppState& s) {
    ProjectState prs{*s.project};
    prs.cursorRow      = s.projectCursorRow;
    prs.cursorColumn   = s.projectCursorColumn;
    prs.isRendering    = s.isRendering;
    prs.renderProgress = s.renderProgress;
    prs.isCollecting    = s.isCollecting;
    prs.collectProgress = s.collectProgress;
    prs.sampleRamBytes = s.sampleRamBytes;
    prs.freeRamBytes   = s.freeRamBytes;
    prs.caps           = s.caps;
    prs.theme          = s.theme;
    return prs;
}

SettingsState settings_state(const AppState& s) {
    SettingsState ss{s.settings};
    ss.cursorRow    = s.settingsCursorRow;
    ss.cursorColumn = s.settingsCursorColumn;
    ss.layoutText   = s.layoutText;
    ss.skinText     = s.skinText;
    ss.overlayText  = s.overlayText;
    ss.audioOutText = s.audioOutText;
    ss.latencyText  = s.latencyText;
    ss.appVersion   = s.appVersion;
    ss.scalingFallback = s.scalingFallback;
    ss.themeName    = s.theme.name;
    ss.caps         = s.caps;
    ss.theme        = s.theme;
    return ss;
}

MidiState midi_state(const AppState& s) {
    MidiState ms{*s.project, s.settings, s.midiDeviceNames, s.midiInDeviceNames};
    ms.lastCcChannel = s.midiInCcChannel;
    ms.clockBpm      = s.midiClockBpm;
    ms.cursorRow     = s.midiCursorRow;
    ms.cursorColumn  = s.midiCursorColumn;
    ms.deviceIndex   = s.midiDeviceIndex;
    ms.inDeviceIndex = s.midiInDeviceIndex;
    ms.outOpenName   = s.midiOutOpenName;
    ms.inOpenName    = s.midiInOpenName;
    ms.autoOffsetMs  = s.midiAutoOffsetMs;
    ms.audioLoad     = s.audioLoad;
    ms.statusText    = s.midiStatusText;
    ms.caps          = s.caps;
    ms.theme         = s.theme;
    return ms;
}

MidiMapState midi_map_state(const AppState& s) {
    MidiMapState mm{*s.project};
    mm.cursorRow    = s.midiMapCursorRow;
    mm.cursorColumn = s.midiMapCursorColumn;
    mm.theme        = s.theme;
    return mm;
}

EqState eq_state(const AppState& s) {
    EqState es{*s.project};
    es.slotIndex     = s.eq.slotIndex;
    es.cursorRow     = s.eq.cursorRow;
    es.caller        = s.eq.caller;
    es.spectrum      = s.eqSpectrum;
    es.spectrumCount = s.eqSpectrumCount;
    es.sampleRate    = static_cast<float>(s.eqSampleRate);
    es.theme         = s.theme;
    return es;
}

ThemeState theme_state(const AppState& s) {
    ThemeState ts;
    ts.theme  = s.theme;
    ts.editor = s.themeEditor;
    return ts;
}

}  // namespace

// ─── The editor, left of the right bar ───────────────────────────────────────────────────────────
// The EQ and theme editors replace the module but leave the furniture: the scope keeps moving under them.
void TrackerLayout::draw_editor(Canvas& c, const AppState& s, int x, int y) {
    constexpr int editorWidth = EDITOR_CLIP_RIGHT - SIDE_SPACER;
    Canvas::ClipScope clip(c, x - SIDE_SPACER, 0, editorWidth + SIDE_SPACER, DESIGN_H);

    if (s.eq.isOpen) {
        eq_.draw(c, x, y, eq_state(s));
        return;
    }
    if (s.themeEditor.isOpen) {
        themeEditor_.draw(c, x, y, theme_state(s));
        return;
    }

    switch (s.currentScreen) {
        case ScreenType::PHRASE:     phraseEditor_.draw(c, x, y, phrase_state(s)); break;
        case ScreenType::CHAIN:      chainEditor_.draw(c, x, y, chain_state(s)); break;
        case ScreenType::SONG:       songEditor_.draw(c, x, y, song_state(s)); break;
        case ScreenType::TABLE:      tableModule_.draw(c, x, y, table_state(s)); break;
        case ScreenType::GROOVE:     grooveModule_.draw(c, x, y, groove_state(s)); break;
        case ScreenType::SCALE:      scaleModule_.draw(c, x, y, scale_state(s)); break;
        case ScreenType::INSTRUMENT: instrumentEditor_.draw(c, x, y, instrument_state(s)); break;
        case ScreenType::INST_POOL:  instrumentPool_.draw(c, x, y, pool_state(s)); break;
        case ScreenType::MODS:       modulation_.draw(c, x, y, mods_state(s)); break;
        case ScreenType::MIXER:      mixer_.draw(c, x, y, mixer_state(s)); break;
        case ScreenType::PERFORM:    draw_perform(c, x, y, s); break;
        case ScreenType::VISUALIZER: draw_visualizer_screen(c, s, x, y); break;
        case ScreenType::EFFECTS:    effects_.draw(c, x, y, effects_state(s)); break;
        case ScreenType::PROJECT:    project_.draw(c, x, y, project_state(s)); break;
        case ScreenType::SETTINGS:   settings_.draw(c, x, y, settings_state(s)); break;
        case ScreenType::MIDI:       midi_.draw(c, x, y, midi_state(s)); break;
        case ScreenType::MIDI_MAP:   midiMap_.draw(c, x, y, midi_map_state(s)); break;
        default:                     draw_placeholder(c, x, y, s.currentScreen, s.theme); break;
    }
}

// Last, over everything — a modal's backdrop dims the whole frame.
void TrackerLayout::draw_overlays(Canvas& c, const AppState& s) {
    const Theme& t = s.theme;
    draw_fx_helper(c, s.fxHelper, t);
    draw_map_picker(c, s.mapPicker, t);
    if (send_editor_drawn(s)) draw_send_editor(c, s.sendEditor, *s.project, t);
    draw_render_dialog(c, s.renderDialog, *s.project, s.isRendering, s.renderProgress, t);
    if (s.qwerty.isOpen) qwerty_.draw(c, s.qwerty, t);
    draw_confirm_dialog(c, s.confirm, t);
}

bool TrackerLayout::has_falling_meters(const AppState& s) const {
    if (!s.project) return false;

    // ⚠️ Same early-outs as `draw()`: these screens hide the scope, MIXER and EQ, so nothing of
    // theirs can fall to rest, and the loop would pin at 60 Hz (SPECTRUM bars frozen by START).
    if (s.splashProgress >= 0 || s.localSampleRange.isOpen || s.scenePicker >= 0) return false;

    // Momentary performance animation continues even for silence-producing gestures such as
    // DROP OUT. Releasing A (or opening help/leaving Punch) immediately restores idle skipping.
    if (s.studioOpen) return s.studioTab == 3 && s.studioAUsed && !s.studioHelp;

    // The EQ editor's panel, asked what it last drew.
    if (s.eq.isOpen && !eq_.spectrum_at_rest()) return true;

    // ⚠️ Only when the mixer is drawn: under an overlay a falling marker never ages and the loop pins
    // at 60 Hz.
    if (s.currentScreen == ScreenType::MIXER && !editor_overlay_up(s) && !mixer_.peaks_at_rest())
        return true;

    // The SPECTRUM bars, likewise only while drawn.
    const VisualizerType vt = s.theme.visualizerType;
    if (vt != VisualizerType::SPECTRUM && vt != VisualizerType::SPECTRUM_PEAKS) return false;
    if (full_screen_module(s) || s.helpOpen || s.helpFull) return false;
    if (s.currentScreen == ScreenType::VISUALIZER && !editor_overlay_up(s) && !visualizer_.bars_at_rest()) return true;
    return !phrase_overview_visible(s) && !oscilloscope_.bars_at_rest();
}

// ─── The status line, top-left of the scope strip ────────────────────────────────────────────────
void TrackerLayout::draw_status_line(Canvas& c, const AppState& s, int scopeY) const {
    if (draw_chord_hint(c, s, scopeY)) return;
    if (s.statusMessage.empty()) {
        statusShown_.clear();   // the same message later enters again
        if (s.settings.helpMode != static_cast<int>(HelpMode::OFF)) {
            c.draw_label("SELECT HELP", SIDE_SPACER + 10, scopeY + 10, s.theme.textParam, 12);
            // A newcomer on an empty song learns that a guided start exists, until it was finished
            // once (firstBeatDone). Never while it runs, never with help switched OFF.
            if (first_beat_invite(s))
                c.draw_label("NEW HERE?  SELECT, THEN START: FIRST BEAT",
                             SIDE_SPACER + 10 + Canvas::label_width("SELECT HELP", 12) + 18, scopeY + 10,
                             s.theme.rowCursor, 12);
        }
        return;
    }

    const std::string text = Canvas::clip_text(s.statusMessage, STATUS_MAX_CHARS);
    const Argb        color = s.statusSuccess ? s.theme.vizWave : error_ink(s.theme);
    // A new message slides in from the left and fades up over 220 ms, a fine line running out under
    // it; an error shakes instead. Only with a running clock and Motion on; a still draws it in place.
    constexpr uint64_t kEnterMs = 220;
    const uint64_t now = s.animationMs;
    const std::string key = (s.statusSuccess ? "+" : "-") + s.statusMessage;
    if (key != statusShown_) {
        statusShown_ = key;
        statusStart_ = now;
        statusUntil_ = now && s.settings.sceneMotion ? now + kEnterMs : 0;
    }
    if (statusUntil_ > now && now >= statusStart_) {
        const float p = float(now - statusStart_) / float(kEnterMs);
        const float e = 1.f - (1.f - p) * (1.f - p) * (1.f - p);
        const int width = Canvas::text_width(text, CHAR_SPACING, FONT_SCALE);
        int dx = s.statusSuccess ? static_cast<int>((1.f - e) * -26.f)
                                 : static_cast<int>(std::sin(p * 18.f) * 5.f * (1.f - p));
        c.draw_text(text, SIDE_SPACER + 10 + dx, scopeY + 10, with_alpha(color, .25f + .75f * e), CHAR_SPACING, FONT_SCALE);
        c.fill_rect(SIDE_SPACER + 10, scopeY + 10 + ROW_HEIGHT - 6, static_cast<int>(width * e), 1,
                    with_alpha(color, .7f * (1.f - p)));
        return;
    }
    c.draw_text(text, SIDE_SPACER + 10, scopeY + 10, color, CHAR_SPACING, FONT_SCALE);
}

// A held on a CHD value: the chord's name and its notes. In a PHRASE, made of this row's note in the
// track's scale as far as this phrase shows it; in a TABLE, on C-4 moved by the row's transpose, as the
// table plays it — unsnapped, and named as the note it lands on.
// False when the cursor is elsewhere.
bool TrackerLayout::draw_chord_hint(Canvas& c, const AppState& s, int scopeY) const {
    if (!s.aHeld || s.selection.active) return false;
    const songcore::Project& p = *s.project;
    int value = -1, root = 60;
    const songcore::Scale* scale = nullptr;
    int key = 0;
    std::string where;   // the root's name, or why there is none
    if (s.currentScreen == ScreenType::PHRASE) {
        const int col = s.cursorColumn;
        if (col != 5 && col != 7 && col != 9) return false;
        const songcore::Phrase&     ph   = p.phrases[static_cast<size_t>(s.currentPhrase)];
        const songcore::PhraseStep& step = ph.steps[static_cast<size_t>(s.cursorRow)];
        const int                   slot = (col - 3) / 2;
        if (songcore::step_fx_type(step, slot) != songcore::FX_CHD) return false;
        value = songcore::step_fx_value(step, slot);
        if (!songcore::note_is_pitch(step.note)) {
            where = "NEEDS A NOTE ON THIS ROW";
        } else {
            const songcore::NoteScale ns = songcore::phrase_note_scale(p, ph, s.cursorRow, step.instrument);
            root  = ns.snap(songcore::note_to_midi(step.note));
            scale = ns.scale;
            key   = ns.key;
        }
    } else if (s.currentScreen == ScreenType::TABLE) {
        const int col = s.tableCursorColumn;
        if (col != 4 && col != 6 && col != 8) return false;
        const songcore::TableRow& r =
            p.tables[static_cast<size_t>(s.currentTable)].rows[static_cast<size_t>(s.tableCursorRow)];
        const int slot = (col - 4) / 2;
        const int type = slot == 0 ? r.fx1Type : slot == 1 ? r.fx2Type : r.fx3Type;
        if (type != songcore::FX_CHD) return false;
        value = slot == 0 ? r.fx1Value : slot == 1 ? r.fx2Value : r.fx3Value;
        // The row's transpose moves the note the chord is built on: 00-7F up, 80-FF down.
        const int tsp = r.transpose < 0x80 ? r.transpose : r.transpose - 256;
        root = std::max(0, std::min(127, 60 + tsp));
    } else {
        return false;
    }

    std::string head  = std::string(songcore::chord_name(value)) + "  " + songcore::chord_layout_name(value);
    std::string notes = where;
    if (where.empty()) {
        if (s.currentScreen == ScreenType::PHRASE) {
            const char* name = songcore::NOTE_NAMES[songcore::scale_mod12(root)];
            head = std::string(name, name[1] == '-' ? 1 : 2) + " " + head;
        } else {
            notes = "ON " + songcore::note_name(songcore::note_from_midi(root)) + ":";
        }
        int semis[songcore::CHORD_MAX_NOTES];
        const int n = songcore::chord_notes(value, root, scale, key, semis);
        for (int i = 0; i < n; ++i) {
            if (!notes.empty()) notes += ' ';
            notes += songcore::note_name(songcore::note_from_midi(root + semis[i]));
        }
    }
    const int x = SIDE_SPACER + 10, y = scopeY + 10;
    c.draw_text(Canvas::clip_text(head, STATUS_MAX_CHARS), x, y, s.theme.vizWave, CHAR_SPACING, FONT_SCALE);
    c.draw_text(Canvas::clip_text(notes, STATUS_MAX_CHARS), x, y + 21, s.theme.textTitle, CHAR_SPACING,
                FONT_SCALE);
    return true;
}

// ─── The selection scope ("SEL:ROW") over the clipboard ("PHR:2x3"), top-right of the strip ──────
void TrackerLayout::draw_selection_clipboard(Canvas& c, const AppState& s, int scopeY) const {
    const std::string sel  = s.selection.info();
    const std::string clip = s.clipboard ? s.clipboard->info() : std::string();
    if (sel.empty() && clip.empty()) return;

    const int x = SIDE_SPACER + OscilloscopeModule::WIDTH - 150;
    const int y = scopeY + 10;

    if (!sel.empty())
        c.draw_text(sel, x, y, s.theme.vizWave, CHAR_SPACING, FONT_SCALE);
    if (!clip.empty())
        c.draw_text(clip, x, sel.empty() ? y : y + 21, s.theme.textTitle, CHAR_SPACING, FONT_SCALE);
}

void TrackerLayout::draw_track_rail(Canvas& c, const AppState& s, int x, int y, bool left) const {
    const Theme&             t = s.theme;
    const songcore::Project& p = *s.project;

    constexpr int height = DESIGN_H - EDITOR_Y - SCREEN_SPACER;
    constexpr int width = NavigationMapModule::WIDTH;
    const bool mixed = t.sceneChrome || light_ground(t.background);
    const Argb panel = mixed ? mix_rgb(t.rowEvery4th, t.background, 42) : darken(t.rowEvery4th, .72f);
    c.panel_rect(x, y, width, height, panel, with_alpha(t.textParam, .2f), 5);
    c.fill_rect(left ? x + width + 3 : x - 4, y, 1, height, with_alpha(t.textParam, .2f));

    const int statusY = y + (left ? 128 : 5);
    c.fill_rect(x + 8, statusY + 3, 5, 5, s.isPlaying ? t.vizWave : t.textEmpty);
    c.draw_text(s.isPlaying ? "PLAYING" : "READY", x + 22, statusY, t.textParam, 1, 2);

    const int bpmRowY = y + (left ? 149 : ROW_HEIGHT + 14);
    const int bpmTextY = bpmRowY + TEXT_PADDING;
    c.draw_text("T>", x + 2, bpmTextY, t.textEmpty, CHAR_SPACING, FONT_SCALE);
    c.draw_text(std::to_string(songcore::tempo_in_force(p)), x + 36, bpmTextY, t.textValue,
                CHAR_SPACING, FONT_SCALE);

    const int trackRowsStartY = bpmRowY + (left ? 29 : ROW_HEIGHT * 2);
    for (int i = 0; i < 8; ++i) {
        const int textY = trackRowsStartY + (i * ROW_HEIGHT) + TEXT_PADDING;
        const auto note = s.trackNotes[i];
        const bool empty = note == songcore::Note::EMPTY();
        const Argb row = empty ? (mixed ? panel : darken(t.rowEvery4th, .8f)) : t.rowEvery4th;

        c.fill_rect(x + 4, textY - 2, width - 8, ROW_HEIGHT - 2, row);
        if (!empty) {
            // While playing, the edge wears the colour of the instrument the track is playing.
            Argb edge = t.vizWave;
            const TrackPlayhead& now = s.playheads[i];
            if (s.isPlaying && now.phraseId >= 0 && now.phraseId < static_cast<int>(p.phrases.size()) && now.step >= 0 &&
                now.step < static_cast<int>(p.phrases[static_cast<size_t>(now.phraseId)].steps.size())) {
                const auto& st = p.phrases[static_cast<size_t>(now.phraseId)].steps[static_cast<size_t>(now.step)];
                if (songcore::step_has_note(st) && st.instrument >= 0) edge = inst_color(t, st.instrument);
            }
            c.fill_rect(x + 4, textY - 2, 2, ROW_HEIGHT - 2, edge);   // 2 px, clear of the track number
        }
        // A trigger LED: each step that plays a note on this track flashes its row for 220 ms.
        {
            constexpr uint64_t kPulseMs = 220;
            const TrackPlayhead& ph = s.playheads[i];
            int key = -1;
            if (s.isPlaying && ph.phraseId >= 0 && ph.phraseId < static_cast<int>(p.phrases.size()) && ph.step >= 0 &&
                ph.step < static_cast<int>(p.phrases[static_cast<size_t>(ph.phraseId)].steps.size())) {
                const auto n = p.phrases[static_cast<size_t>(ph.phraseId)].steps[static_cast<size_t>(ph.step)].note;
                if (n != songcore::Note::EMPTY() && n != songcore::Note::OFF()) key = ph.phraseId * 256 + ph.step;
            }
            const uint64_t now = s.animationMs;
            if (key != railStep_[i]) {
                railStep_[i] = key;
                if (key >= 0 && now && s.settings.sceneMotion) {
                    railStart_[i] = now;
                    railUntil_ = std::max(railUntil_, now + kPulseMs);
                }
            }
            if (now && railStart_[i] && now < railStart_[i] + kPulseMs) {
                const float q = 1.f - float(now - railStart_[i]) / float(kPulseMs);
                c.fill_rect(x + 4, textY - 2, width - 8, ROW_HEIGHT - 2, with_alpha(t.vizWave, .30f * q * q));
                c.fill_rect(x + 4, textY - 2, 4, ROW_HEIGHT - 2, with_alpha(t.textValue, .8f * q));
            }
        }

        c.draw_text(std::to_string(i + 1), x + (t.sceneChrome ? 8 : 2), textY, t.textParam, CHAR_SPACING, FONT_SCALE);
        c.draw_text(note_name(note), x + 36, textY, empty ? t.textEmpty : t.textValue,
                    CHAR_SPACING, FONT_SCALE);
    }

    const int markY = y + (left ? 356 : 255);
    if (t.sceneChrome) {
        brand::mark(c, x + 10, markY, 20, t.rowCursor, with_alpha(t.textTitle, .35f));
        const int family = static_cast<int>(scene_look(t.sceneChrome - 1).family);
        const Argb ink = with_alpha(t.rowCursor, .55f);
        const int markX = x + 42;
        // While playing every family animates its own pattern (ui/scene_animation.h).
        const int bpm = songcore::tempo_in_force(p);
        int playStep = -1;
        for (const auto& head : s.playheads) if (head.step >= 0) { playStep = head.step; break; }
        namespace sa = scene_animation;
        const bool animate = s.isPlaying && s.settings.sceneMotion;
        if (animate && (family == 0 || family == 4)) { sa::dot_matrix(c, markX, markY, t, s.animationMs, bpm, family == 4); }
        else if (animate && family == 1) { sa::ascii_meter(c, markX, markY, t, s.animationMs, bpm); }
        else if (animate && family == 2) { sa::dos_progress(c, markX + 2, markY + 3, t, playStep, 16); }
        else if (animate && family == 3) { sa::terminal_prompt(c, markX, markY, t, s.animationMs, bpm); }
        else switch (family % 4) {
            case 0:
                for (int i = 0; i < 11; ++i)
                    for (int row = 0; row < 3; ++row)
                        if ((i * i + row * 7 + family) % 5 < 3)
                            c.fill_rect(markX + i * 5, markY + 2 + row * 5, 3, 3, ink);
                break;
            case 1:
                for (int i = 0; i < 6; ++i)
                    c.fill_rect(markX, markY + i * 3, 38 - ((i * 19) % 28), 1, ink);
                break;
            case 2:
                for (int i = 0; i < 7; ++i)
                    c.fill_rect(markX + i * 7, markY + 2 + i % 2 * 7, 5, 7, ink);
                break;
            case 3:
                // VECTOR while playing: a turning wireframe cube; otherwise the static zigzag.
                if (family == scene_animation::VECTOR && s.isPlaying && s.settings.sceneMotion) {
                    scene_animation::wire_cube(c, markX + 28, markY + 10, 8.f, with_alpha(t.rowCursor, .85f),
                                               s.animationMs, songcore::tempo_in_force(p));
                    break;
                }
                for (int i = 0; i < 4; ++i) {
                    c.line(markX + i * 11, markY + 17, markX + i * 11 + 11, markY + 2, ink);
                    c.line(markX + i * 11 + 11, markY + 2, markX + i * 11 + 21, markY + 17, ink);
                }
                break;
        }
    } else if (brand::expressive_material(t)) {
        brand::mark(c, x + 12, markY, 21, t.rowCursor, with_alpha(t.textTitle, .35f));
        c.draw_label("BUNKA LAB", x + 41, markY + 4, t.textParam, 12);
        brand::registration(c, x + 12, markY + 25, 91, t, s.blinkPhaseMs, s.isPlaying);
    }

    NavigationMapState ns;
    ns.currentScreen      = s.currentScreen;
    ns.sourceColumn       = s.previousColumn;
    ns.instrumentFromPool = s.instrumentFromPool;
    ns.theme              = t;
    if (s.navBannerUntilMs > s.animationMs) {
        // 900 ms in all: full for 650, then a 250 ms fade.
        const float remainingMs = float(s.navBannerUntilMs - s.animationMs);
        ns.bannerLevel = std::min(1.f, remainingMs / 250.f);
    }
    navigationMap_.draw(c, x, y + (left ? 10 : height - NavigationMapModule::HEIGHT), ns);
}

void TrackerLayout::draw_placeholder(Canvas& c, int x, int y, ScreenType screen,
                                     const Theme& t) const {
    c.fill_rect(x, y, OscilloscopeModule::WIDTH, 392, t.background);

    c.draw_text(screen_label(screen), x + 20, y + TEXT_PADDING, t.textTitle, CHAR_SPACING,
                FONT_SCALE);

    const std::string message = "COMING SOON";
    const int         msgW    = Canvas::text_width(message, CHAR_SPACING, FONT_SCALE);
    c.draw_text(message, x + (OscilloscopeModule::WIDTH - msgW) / 2, y + 180, t.textEmpty,
                CHAR_SPACING, FONT_SCALE);
}

}  // namespace pt::ui
