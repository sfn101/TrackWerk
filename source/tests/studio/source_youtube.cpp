// ptsource-youtube-check: YouTube in the Source Sampler is opt-in. It starts off, A twice on
// Provider = YouTube turns it on, A once turns it off, searches are refused while off, and the
// choice is saved in settings.json only when on.
// usage: ptsource-youtube-check <new run directory>
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type"
#define main save_io_check_main
#include "sample_editor_save_io.cpp"
#undef main
#pragma clang diagnostic pop
#include "ui/settings_store.h"
#include "ui/source_sampler.h"
#include "ui/studio.h"

namespace {
namespace fs = std::filesystem;

struct FixedBackend : pt::ui::SourceBackend {
    std::string snapshot;
    std::vector<std::string> commands;
    bool command(const std::string& action, const std::string& args) override {
        commands.push_back(action + " " + args);
        return true;
    }
    std::string state_json() override { return snapshot; }
};

// Through studio_button, as the app delivers it, so the setting mirror is exercised.
void tap(pt::ui::Button b, Sample& sample) {
    pt::ui::ButtonEvent e;
    e.button = b;
    e.action = pt::ui::ButtonAction::PRESSED;
    pt::ui::studio_button(e, sample.state, sample.host, *sample.engine, sample.fs);
    if (b == pt::ui::Button::A) {
        e.action = pt::ui::ButtonAction::RELEASED;
        pt::ui::studio_button(e, sample.state, sample.host, *sample.engine, sample.fs);
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2 || fs::exists(argv[1])) {
        std::cerr << "usage: ptsource-youtube-check <new run directory>\n";
        return 2;
    }
    Sample sample(fs::path(argv[1]), true, 48000, 256, 1);
    auto& st = sample.state;
    assert(!st.settings.sourceYoutube);   // default off
    FixedBackend backend;
    backend.snapshot = songcore::json{{"status", "idle"}, {"providers", songcore::json::array({
        {{"id", "archive"}, {"available", true}}, {{"id", "commons"}, {"available", true}},
        {{"id", "youtube"}, {"available", true}}})}}.dump();
    auto& s = st.sourceSampler;
    s.backend = &backend;
    st.studioOpen = true;
    st.studioTab = 7;
    st.currentScreen = pt::ui::ScreenType::PROJECT;
    assert(pt::ui::source_sampler_poll(s, 1000));

    // Off: YouTube can be chosen on Provider, but Search refuses and sends nothing.
    s.provider = pt::ui::SOURCE_YOUTUBE_PROVIDER;
    s.row = 2;
    s.col = 0;
    backend.commands.clear();
    tap(pt::ui::Button::A, sample);
    assert(backend.commands.empty() && s.status == "YouTube is off. On Provider, press A twice to opt in.");

    // A once warns, a second A turns it on and into the saved settings.
    s.row = 0;
    tap(pt::ui::Button::A, sample);
    assert(!st.settings.sourceYoutube && s.youtubeConfirm && s.status.find("yt-dlp") != std::string::npos);
    tap(pt::ui::Button::A, sample);
    assert(st.settings.sourceYoutube && s.youtubeEnabled && s.provider == pt::ui::SOURCE_YOUTUBE_PROVIDER);
    s.row = 2;
    tap(pt::ui::Button::A, sample);
    assert(!backend.commands.empty() && backend.commands.back().find("query_dialog") == 0 &&
           backend.commands.back().find("youtube") != std::string::npos);

    // Saved only when on; read back after a restart.
    assert(pt::ui::save_settings(sample.fs, st.settings, st.theme));
    std::string raw;
    assert(sample.fs.read_file(sample.fs.settings_path(), raw) && raw.find("\"sourceYoutube\": true") != std::string::npos);
    pt::ui::SettingsValues loaded;
    pt::ui::Theme theme;
    assert(pt::ui::load_settings(sample.fs, loaded, theme) && loaded.sourceYoutube);

    // Leaving YouTube with Left/Right resets a pending confirmation; A once turns it off again.
    s.row = 0;
    tap(pt::ui::Button::A, sample);
    assert(!st.settings.sourceYoutube && !s.youtubeEnabled);
    tap(pt::ui::Button::A, sample);   // arms
    assert(s.youtubeConfirm);
    tap(pt::ui::Button::DPAD_RIGHT, sample);
    assert(!s.youtubeConfirm && s.provider != pt::ui::SOURCE_YOUTUBE_PROVIDER);
    assert(pt::ui::save_settings(sample.fs, st.settings, st.theme));
    assert(sample.fs.read_file(sample.fs.settings_path(), raw) && raw.find("sourceYoutube") == std::string::npos);
    // KORREKTURAUFTRAG-CLAUDE-44: off, YouTube results still listed from an earlier search: opening
    // one sends nothing (it would resolve and download), and the search callback refuses too.
    backend.snapshot = songcore::json{{"status", "results"}, {"provider", "youtube"},
        {"results", songcore::json::array({{{"id", "yt1"}, {"title", "Old hit"}}})},
        {"tracks", songcore::json::array({{{"id", "yt1"}, {"title", "Old hit"}}})}}.dump();
    s.hasPolled = false;
    assert(pt::ui::source_sampler_poll(s, 5000) && s.catalogProvider == "youtube" && !st.settings.sourceYoutube);
    backend.commands.clear();
    for (int row : {3, 4}) {
        s.row = row;
        s.listFocused = true;
        s.busy = false;
        tap(pt::ui::Button::A, sample);
    }
    assert(backend.commands.empty() && s.status.find("YouTube is off") == 0);
    s.provider = pt::ui::SOURCE_YOUTUBE_PROVIDER;
    assert(!pt::ui::source_sampler_submit_query(s, "callback while off") && backend.commands.empty());
    // Other providers' results still open.
    backend.snapshot = songcore::json{{"status", "results"}, {"provider", "archive"},
        {"results", songcore::json::array({{{"id", "ia1"}, {"title", "Archive hit"}}})}}.dump();
    s.hasPolled = false;
    assert(pt::ui::source_sampler_poll(s, 6000));
    s.row = 3; s.listFocused = true; s.busy = false;
    tap(pt::ui::Button::A, sample);
    assert(!backend.commands.empty() && backend.commands.back().find("open") == 0);

    // specs/youtube-picker.md: Search > PASTE LINK opens a field that holds the controller until A or B.
    static std::string clipboard;
    s.readClipboard = [] { return clipboard; };
    const auto press = [&](pt::ui::Button b) {
        pt::ui::ButtonEvent e;
        e.button = b;
        for (auto action : {pt::ui::ButtonAction::PRESSED, pt::ui::ButtonAction::RELEASED}) {
            e.action = action;
            pt::ui::studio_button(e, st, sample.host, *sample.engine, sample.fs);
        }
    };
    st.settings.sourceYoutube = true;
    s.provider = pt::ui::SOURCE_YOUTUBE_PROVIDER; s.row = 2; s.col = 2; s.listFocused = false; s.busy = false;
    clipboard = "listen https://youtu.be/dQw4w9WgXcQ?si=x";
    backend.commands.clear();
    press(pt::ui::Button::A);
    assert(s.linkOpen && pt::ui::source_sampler_link_open(st) && s.link == "https://youtu.be/dQw4w9WgXcQ?si=x" && backend.commands.empty());
    for (auto b : {pt::ui::Button::DPAD_UP, pt::ui::Button::DPAD_LEFT, pt::ui::Button::L, pt::ui::Button::R,
                   pt::ui::Button::START, pt::ui::Button::SELECT})
        press(b);
    assert(s.linkOpen && s.row == 2 && s.col == 2 && !s.showHelp && !st.studioHelp && st.studioOpen && st.studioTab == 7 &&
           backend.commands.empty());
    press(pt::ui::Button::A);
    assert(!s.linkOpen && !backend.commands.empty() && backend.commands.back().find("search") == 0 &&
           backend.commands.back().find("youtu.be/dQw4w9WgXcQ") != std::string::npos);
    s.busy = false;
    press(pt::ui::Button::A);   // reopen, then B closes and sends nothing
    backend.commands.clear();
    press(pt::ui::Button::B);
    assert(!s.linkOpen && backend.commands.empty() && st.studioOpen);
    // An Android share opens the field once, on YouTube.
    backend.snapshot = songcore::json{{"status", "idle"}, {"shared_link", "https://youtu.be/abcdefghijk"}, {"shared_link_id", 42},
        {"providers", songcore::json::array({{{"id", "archive"}, {"available", true}}, {{"id", "commons"}, {"available", true}},
            {{"id", "youtube"}, {"available", true}}})}}.dump();
    s.provider = 0; s.hasPolled = false;
    assert(pt::ui::source_sampler_poll(s, 7000) && s.linkOpen && s.link == "https://youtu.be/abcdefghijk" &&
           s.provider == pt::ui::SOURCE_YOUTUBE_PROVIDER);
    press(pt::ui::Button::B);
    s.hasPolled = false;
    pt::ui::source_sampler_poll(s, 8000);
    assert(!s.linkOpen);
    s.backend = nullptr;
    std::cout << "ptsource-youtube-check: off by default, A A opts in, A opts out, saved only when on; old YouTube results and the search callback refused while off; the link field holds the controller, A sends, B closes, a share opens it once\n";
    return 0;
}
