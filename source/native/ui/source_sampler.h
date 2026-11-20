#pragma once
#include "ui/buttons.h"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class AudioEngine;
namespace songcore { class SongcoreHost; struct Project; }

namespace pt::ui {
class Canvas;
class FileSystem;
struct AppState;
struct Theme;

class SourceBackend {
public:
    virtual ~SourceBackend() = default;
    virtual bool command(const std::string& action, const std::string& argsJson) = 0;
    virtual std::string state_json() = 0;
    // Native platforms use the tracker keyboard; Android retains its own dialog.
    virtual bool take_query_request() { return false; }
};

inline constexpr int SOURCE_QUICK_CLIP_ROW = 17;
inline constexpr int SOURCE_COLLECTION_ROW = 18;
// Hides results a release cannot use (source_credits.h); 0 all, 1 release-safe, 2 no credit needed.
inline constexpr int SOURCE_LICENSE_ROW = 19;
// Sets OUT a whole number of bars (4/4) after IN at the song's tempo.
inline constexpr int SOURCE_BARS_ROW = 20;
inline constexpr int SOURCE_SAMPLER_ROWS = SOURCE_BARS_ROW + 1;
inline constexpr int SOURCE_BAR_LENGTHS[] = {1, 2, 4, 8};
inline constexpr int SOURCE_SAMPLER_PROVIDERS = 5;
#ifdef PT_MIYOO_PLUS_ARCHIVE
inline constexpr int64_t SOURCE_CLIP_MAX_MS = 60000;
#else
inline constexpr int64_t SOURCE_CLIP_MAX_MS = 180000;
#endif

struct SourceSamplerItem {
    std::string id, title, creator, kind, url, license;
    int64_t durationMs = 0;
    std::string description,tags,instruments;
    bool live = false;
};

struct SourceSamplerState {
    struct ClipRequest {
        int64_t inMs, outMs;
        uint64_t startedMs;
    };

    SourceBackend* backend = nullptr; // Owned by the platform shell, never by AppState copies.
    int row = SOURCE_COLLECTION_ROW, col = 0, provider = 0, genre = 0;
    int selectedResult = -1, selectedTrack = -1, targetInstrument = -1;
    int openedResult = -1, playingTrack = -1;
    std::array<bool, SOURCE_SAMPLER_PROVIDERS> providerAvailable = {{true}};
    std::vector<SourceSamplerItem> results, tracks;
    std::string query, title, creator, sourceUrl, licenseUrl;
    std::string description,tags,instruments;
    std::string backendStatus = "idle", status = "Search for music, then choose a source and track.";
    std::string error, clipPath, importPath, invalidatedClipPath;
    int64_t positionMs = 0, durationMs = 0, inMs = 0, outMs = 0, clipFrames = 0;
    int64_t liveWindowStartMs = 0, liveWindowEndMs = 0, livePlayheadMs = 0;
    int clipRate = 0, clipChannels = 0;
    bool playing = false, recording = false, rendering = false, busy = false, clipReady = false;
    bool importRequested = false, showHelp = false, aUsed = false;
    bool listFocused = false, pendingTrackList = false, sortTitle = false;
    bool showingTracks = false, live = false, liveAvailable = false, previewGainAvailable = false, favoritesAvailable = false;
    bool liveHistoryAvailable = false, liveRewound = false, liveInSet = false, liveOutSet = false;
    bool openWhenReady = false;
    bool importContextBound = false, importIsAutomatic = false;
    bool takeRequestIdsAvailable = false;
    // A file that plays while it downloads (backend capability progressive_mp3, Linux 1.2). durationMs
    // is then the loaded length; totalMs the catalogue's (0 unknown). Backends without the fields
    // (Android MediaPlayer, older builds) leave these false / 0 and nothing changes for them.
    bool progressiveAvailable = false, downloading = false;
    int64_t totalMs = 0;
    const songcore::Project* importProject = nullptr; // Compared only, never dereferenced.
    int importProjectVersion = 0;
    int importInstrument = -1;
    uint32_t nextTakeRequest = 0, expectedTakeRequest = 0;
    int previewGainPercent = 100, liveCaptureSeconds = 30;
    int quickClipLengthIndex = 2, historyTakeLengthIndex = 2, liveHistorySeconds = 0;
    int licenseFilter = 0;   // SourceLicenseFilter
    int barLengthIndex = 1;  // SOURCE_BAR_LENGTHS: 2 bars
    // Mirrors SettingsValues::sourceYoutube (studio.cpp copies it in and out around each button).
    bool youtubeEnabled = false;
    bool youtubeConfirm = false;   // A once on Provider = YouTube: the next A opts in
    // The YouTube link field (specs/youtube-picker.md): Search > PASTE LINK, or an Android share.
    // While it is open it takes every button: A pastes / opens, B closes, the rest do nothing.
    bool linkOpen = false;
    std::string link;
    std::string (*readClipboard)() = nullptr;   // the shell's system clipboard; null where there is none
    int64_t sharedLinkId = 0;                   // the last Android share already taken
    int collectionProvider = 0, shelfGroup = 0, shelfCursor = 0, stationGenre = 0;
    bool collectionOpen = false, collectionArmed = false;
    bool quickClipArmed = false;
    std::optional<ClipRequest> quickClipRequest;
    std::string catalogProvider, catalogGenre, catalogCollection, shelfTitle;
    double progress = 0;
    // Provider / Genre as a list (1.2): A opens it, Up / Down choose, A takes the choice, B keeps
    // the old one. 0 closed, 1 provider, 2 genre.
    int choiceList = 0, choiceCursor = 0;
    // The slot a take goes to when Target slot is "Current instrument" (studio.cpp mirrors it in),
    // and an import into an occupied slot armed by a first A (the second replaces it).
    int currentSlot = 0;
    bool replaceArmed = false;
    // What the armed question was about: the second A replaces only this slot with this take in
    // this project version. Anything else asks again (Codex, STUFE2A-BEFUND-HILFE-BESTAETIGUNG).
    int armedSlot = -1;
    std::string armedClip;
    const songcore::Project* armedProject = nullptr;   // compared only
    int armedVersion = -1;
    uint64_t lastPollMs = 0;
    bool hasPolled = false;
    std::string lastSnapshot;
};

inline constexpr int source_sampler_row_count() { return SOURCE_SAMPLER_ROWS; }
// True while the YouTube link field is open on Studio > Sources: no other shortcut may run.
bool source_sampler_link_open(const AppState&);
const char* source_sampler_genre_name(int genre);
const char* source_sampler_provider_name(int provider);
// At most one backend snapshot per100ms. Draws consume only this parsed cache.
bool source_sampler_poll(SourceSamplerState&, uint64_t nowMs);
void source_sampler_stop(SourceSamplerState&);
// Drop a pending "A again replaces" question. Studio-wide: help, leaving Sources, focus or controller
// loss, a cancelled button. Playback and radio are left alone.
void source_sampler_disarm(SourceSamplerState&);
bool source_sampler_submit_query(SourceSamplerState&, const std::string& query);
bool source_sampler_button(const ButtonEvent&, SourceSamplerState&, songcore::SongcoreHost&, AudioEngine&);
void source_sampler_bind_import_context(AppState&);
bool source_sampler_apply_import(AppState&, songcore::SongcoreHost&, AudioEngine&, FileSystem&);
// The length of `bars` 4/4 bars at `tempo` BPM in milliseconds, rounded to the nearest.
int64_t source_sampler_bars_ms(int bars, int tempo);
// The YouTube provider's index; it is usable only when youtubeEnabled.
inline constexpr int SOURCE_YOUTUBE_PROVIDER = 2;
// Whether a result or title passes the licence filter.
bool source_sampler_item_shown(const SourceSamplerState&, const SourceSamplerItem&);
void source_sampler_draw(Canvas&, const SourceSamplerState&, const Theme&, const songcore::Project&);
// The same, with the current instrument (Target slot shows where a take lands and if it is in use).
void source_sampler_draw(Canvas&, const SourceSamplerState&, const Theme&, const songcore::Project&, int currentSlot);
// The four orientation phases (FIND, LISTEN, MARK, USE) of a control row; not a wizard.
int source_sampler_row_phase(int row);
// The slot a take lands in: the Target slot, or the current instrument.
inline int source_sampler_take_slot(const SourceSamplerState& s) { return s.targetInstrument >= 0 ? s.targetInstrument : s.currentSlot; }
} // namespace pt::ui
