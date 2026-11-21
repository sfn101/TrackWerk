#pragma once
#include "ui/audio_capture.h"
#include <memory>
#include <string>

namespace ptshell {
// Preserves the existing microphone/USB-interface path and adds the explicitly
// experimental Apple direct-USB path. Only one backend can record at a time.
class CaptureRouter final : public pt::ui::CaptureBackend {
public:
    explicit CaptureRouter(std::string appRoot);
    ~CaptureRouter() override;
    std::string inputs_json() override;
    bool start(int deviceId, int channels, int rate) override;
    void stop() override;
    std::string state_json() override;
    // Monitoring is the SDL recorder's; the phone helper owns its device and cannot share the output.
    bool set_monitor(bool on) override;
    // SdlAudioEngine's monitor hook. Never locks or allocates: it drains the recorder's SPSC ring.
    static void monitor_hook(void* self, float* stereo, int frames, int sampleRate);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
