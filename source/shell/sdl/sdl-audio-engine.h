// The SDL2 audio backend: opens a stereo float stream and hands each buffer to
// AudioEngine::processLiveBlock(). ⚠️ Glue only — no DSP here.

#ifndef POCKETTRACKER_SDL_AUDIO_ENGINE_H
#define POCKETTRACKER_SDL_AUDIO_ENGINE_H

// <cmath> before <SDL.h>: the engine defines _USE_MATH_DEFINES (MSVC's M_PI) and SDL_stdinc.h defines
// M_PI behind an #ifndef — the other order warns C4005 on every MSVC build.
#include <cmath>

#include <SDL.h>

#include "audio-backend.h"

#include <string>

class AudioEngine;

class SdlAudioEngine : public AudioBackend {
  public:
    explicit SdlAudioEngine(AudioEngine* core);
    ~SdlAudioEngine() override;

    // Stereo float32 or false — processAudioBlock is stereo-only, and an SDL conversion would be worse
    // than failing loudly.
    bool openStream() override;
    void closeStream() override;

    // AudioEngine::onResumeRequested: unpause the device.
    void resumeStream() override;

    // For an offline render. ⚠️ `renderOffline` rewrites the buffers the callback reads; pausing blocks
    // until the callback has returned, so one writer by construction, not by timing.
    void setPaused(bool paused) override;

    // The negotiated rate, not the one asked for.
    int sampleRate() const override { return sampleRate_; }

    // Always a floor (`measured` false): SDL2 cannot report device latency, so this is the app's own
    // buffer and the driver's queue is left to a microphone.
    OutputLatency outputLatency() const override { return {bufferFrames_, false}; }

    // Open at `hz` from now on; 0 = the engine's rate (or the env override). For a mid-session reopen:
    // loaded samples are pitched for the rate in force.
    void pin_rate(int hz) { pinnedRate_ = hz; }

    // A source preview shares the already-open output device. Changing the hook waits
    // for any current callback; the hook itself must not allocate, lock or perform I/O.
    using ExternalAudio = void (*)(void*, float*, int, int);
    void set_external_audio(ExternalAudio hook, void* userdata);

    // A second mixer, after the external one: input monitoring (the recorder's ring). Separate so a
    // source preview and a live input can sound at once; both must only add to the buffer.
    void set_monitor_audio(ExternalAudio hook, void* userdata);

    // Strict: an AUDIODEV that will not open fails the open, instead of the usual quiet retry on the
    // default device (Linux USB outputs: a USB card that fails must not look like it plays).
    void set_strict_route(bool strict) { strictRoute_ = strict; }
    // The AUDIODEV the last successful open used ("" = SDL's default device): the route that plays.
    const std::string& opened_audiodev() const { return openedAudiodev_; }

  private:
    static void SDLCALL audioCallback(void* userdata, Uint8* out, int lenBytes);

    AudioEngine*      core_         = nullptr;
    SDL_AudioDeviceID device_       = 0;
    int               sampleRate_   = 0;
    int               channels_     = 0;
    int               bufferFrames_ = 0;  // what the device chose
    int               pinnedRate_   = 0;
    ExternalAudio     externalAudio_ = nullptr;
    void*             externalUser_ = nullptr;
    ExternalAudio     monitorAudio_  = nullptr;
    void*             monitorUser_   = nullptr;
    bool              strictRoute_  = false;
    std::string       openedAudiodev_;
};

#endif  // POCKETTRACKER_SDL_AUDIO_ENGINE_H
