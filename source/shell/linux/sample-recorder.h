#pragma once

#include "ui/audio_capture.h"

#include <memory>
#include <string>

namespace ptshell {

// SDL capture for the Linux/PortMaster shell. SDL_INIT_AUDIO must outlive this
// object. Discovery is lazy and cached; driver enumeration, opening, PCM reads,
// closing and disk writes run on background workers, never on the UI caller.
// Stop requests cancellation but does not pretend a blocked OS driver is closed.
// The recorded take still never feeds the output, but an explicit monitor path exists: the worker
// fills a lock-free ring and the output callback drains it (`set_monitor` / `render_monitor`).
class SdlSampleRecorder final : public pt::ui::CaptureBackend {
public:
    explicit SdlSampleRecorder(std::string appRoot);
    ~SdlSampleRecorder() override;
    SdlSampleRecorder(const SdlSampleRecorder&) = delete;
    SdlSampleRecorder& operator=(const SdlSampleRecorder&) = delete;

    std::string inputs_json() override;
    bool start(int deviceId, int channels, int rate) override;
    void stop() override;
    std::string state_json() override;
    // Monitoring: the capture worker fills an SPSC ring and the output callback drains it. No lock, no
    // allocation and no second device on the audio thread. Recording stop turns it off.
    bool set_monitor(bool on) override;
    void render_monitor(float* stereo, int frames, int sampleRate);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ptshell
