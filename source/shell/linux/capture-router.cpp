#include "linux/capture-router.h"
#include "linux/sample-recorder.h"
#ifndef _WIN32
#include "linux/iphone-recorder.h"   // the iPhone USB recorder is Linux-only
#endif
#include "vendor/nlohmann/json.hpp"
#include <mutex>

namespace ptshell {
using Json = nlohmann::json;
struct CaptureRouter::Impl {
#ifdef _WIN32
    explicit Impl(const std::string& root): sdl(root), selected(&sdl) {}
    SdlSampleRecorder sdl;
#else
    explicit Impl(const std::string& root): sdl(root), iphone(root), selected(&sdl) {}
    SdlSampleRecorder sdl;
    IphoneSampleRecorder iphone;
#endif
    pt::ui::CaptureBackend* selected;
    std::mutex lifecycle;
};
CaptureRouter::CaptureRouter(std::string root): impl_(std::make_unique<Impl>(root)) {}
CaptureRouter::~CaptureRouter() = default;
std::string CaptureRouter::inputs_json() {
    Json result = Json::array();
#ifdef _WIN32
    for (auto* backend : {static_cast<pt::ui::CaptureBackend*>(&impl_->sdl)}) {
#else
    for (auto* backend : {static_cast<pt::ui::CaptureBackend*>(&impl_->sdl),
                          static_cast<pt::ui::CaptureBackend*>(&impl_->iphone)}) {
#endif
        try {
            auto rows = Json::parse(backend->inputs_json());
            if (rows.is_array()) for (auto& row : rows) result.push_back(std::move(row));
        } catch (...) {}
    }
    return result.dump();
}
bool CaptureRouter::start(int id, int channels, int rate) {
    std::lock_guard<std::mutex> lock(impl_->lifecycle);
    try {
        const auto state = Json::parse(impl_->selected->state_json()).value("state", "idle");
        if (state == "starting" || state == "recording" || state == "stopping") return false;
    } catch (...) { return false; }
#ifndef _WIN32
    impl_->selected = id >= IPHONE_CAPTURE_ACTION ? static_cast<pt::ui::CaptureBackend*>(&impl_->iphone)
                                                : static_cast<pt::ui::CaptureBackend*>(&impl_->sdl);
#endif
    return impl_->selected->start(id, channels, rate);
}
void CaptureRouter::stop() {
    std::lock_guard<std::mutex> lock(impl_->lifecycle); impl_->selected->stop();
}
std::string CaptureRouter::state_json() {
    std::lock_guard<std::mutex> lock(impl_->lifecycle); return impl_->selected->state_json();
}
bool CaptureRouter::set_monitor(bool on) {
    // Only the SDL recorder has a monitor ring. Turning it off is always safe, even while the phone
    // backend is selected, so a stale "on" cannot survive a source switch.
#ifndef _WIN32
    if (on && impl_->selected == &impl_->iphone) return false;
#endif
    return impl_->sdl.set_monitor(on);
}
void CaptureRouter::monitor_hook(void* self, float* stereo, int frames, int sampleRate) {
    static_cast<CaptureRouter*>(self)->impl_->sdl.render_monitor(stereo, frames, sampleRate);
}
}
