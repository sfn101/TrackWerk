#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include "synth_params.h"
namespace studio {
constexpr int variations = 4; // Legacy URI stride; never change.
int variation_count();
int preset_code(int model,int variation);
int preset_model(int code);
int preset_variation(int code);
std::array<int,3> preset_macros(int code);
// IDs 0..81 are the original Mutable models; 82..93 add Elements articulations,
// 94..101 synthetic granular oscillators, 102 onward original Studio algorithms.
// Synth codes are persisted in project and preset URIs (/@mutable/N.wav, /@mutable/pM-a-b-c.wav),
// so their layout never changes; it only grows into bits that were always zero:
//   factory, legacy    model*4 + variation(0..3)
//   factory, extended  (1<<29) | model<<4 | variation(4..15)         model: bits 4..11
//   custom             (1<<30) | model&127 | a<<7 | b<<14 | c<<21 | (model>>7)<<28   (a,b,c: 7 bit)
// Up to 1.1.0-dev.2 the model fields were 7 bits wide and bit 11 / bit 28 were zero, so every
// existing code decodes unchanged.
inline constexpr int kMaxModels = 256;
inline constexpr int kExtendedFlag = 1 << 29;
inline constexpr int kCustomFlag = 1 << 30;
inline constexpr int encode_extended(int model, int variation) { return kExtendedFlag | (model << 4) | variation; }
inline constexpr int extended_model(int code) { return (code >> 4) & 255; }
inline constexpr int encode_custom(int model, int a7, int b7, int c7) {
    return kCustomFlag | (model & 127) | (a7 << 7) | (b7 << 14) | (c7 << 21) | (((model >> 7) & 1) << 28);
}
inline constexpr int custom_model(int code) { return (code & 127) | (((code >> 28) & 1) << 7); }
inline constexpr int custom_macro(int code, int index) { return (code >> (7 + 7 * index)) & 127; }
inline constexpr int kOctaveOrbitModel = 112;
inline constexpr int kGravityFirstModel = 113;
inline constexpr int kGravityModelCount = 3;
inline constexpr bool is_gravity_model(int model) {
    return model >= kGravityFirstModel && model < kGravityFirstModel + kGravityModelCount;
}
// The Spectral family (Studio > Synth > Spectral): 116 SP ADDITIVE (spectral_additive.h),
// 117 SP FREEZE (spectral_freeze.h), 118 SP MORPH (spectral_morph.h).
inline constexpr int kSpectralFirstModel = kGravityFirstModel + kGravityModelCount;
inline constexpr int kSpectralModelCount = 3;
inline constexpr int kSpectralAdditiveModel = kSpectralFirstModel;
inline constexpr int kSpectralFreezeModel = kSpectralFirstModel + 1;
inline constexpr int kSpectralMorphModel = kSpectralFirstModel + 2;
// SP FREEZE reads a sample slot; its parameter 2 names it (studio::synth_param_*).
inline constexpr int kFreezeSourceParam = 2;
inline constexpr bool is_spectral_model(int model) {
    return model >= kSpectralFirstModel && model < kSpectralFirstModel + kSpectralModelCount;
}
// The Voice family (Studio > Synth > Voice): 119 FORMANT CHOIR (formant_choir.h).
inline constexpr int kVoiceFirstModel = kSpectralFirstModel + kSpectralModelCount;
inline constexpr int kVoiceModelCount = 1;
inline constexpr int kFormantChoirModel = kVoiceFirstModel;
inline constexpr bool is_voice_model(int model) {
    return model >= kVoiceFirstModel && model < kVoiceFirstModel + kVoiceModelCount;
}
// The Vintage family (Studio > Synth > Vintage): 120 SID CHIP (sid_chip.h), 121 DCO POLY (dco_poly.h),
// 122 FM SYSEX (fm_sysex_voice.h), 123 DUB SIREN (dub_siren.h).
inline constexpr int kVintageFirstModel = kVoiceFirstModel + kVoiceModelCount;
inline constexpr int kVintageModelCount = 8;
inline constexpr int kSidChipModel = kVintageFirstModel;
inline constexpr int kDcoPolyModel = kVintageFirstModel + 1;
inline constexpr int kFmSysexModel = kVintageFirstModel + 2;
inline constexpr int kDubSirenModel = kVintageFirstModel + 3;
inline constexpr int kFrogVoiceModel = kVintageFirstModel + 4;   // 124 CROAKER (frog_voice.h)
inline constexpr int kSingFrogModel = kVintageFirstModel + 5;    // 125 FROGGY (sing_frog.h): sings typed words
inline constexpr int kAcidBoxModel = kVintageFirstModel + 6;     // 126 ACID BOX (bass_synths.h)
inline constexpr int kReeseBassModel = kVintageFirstModel + 7;   // 127 REESE BASS (bass_synths.h)
// FROGGY parameters: 2 Lyrics, 3 Voice, 4 Croak, 5 Vibrato, 6 Glide, 7 Coda, 8 Syllable (0 = next one per note)
// (a row that opens the keyboard; the text itself belongs to the instrument, not to the parameters).
inline constexpr int kSingLyricParam = 2;
inline constexpr int kSingVoiceParam = 3;
inline constexpr int kSingSyllableParam = 8;
inline constexpr int kSingPitchParam = 9;   // 0 Note: the note sets the pitch; 1 Mic: the pitch the microphone hears (studio/mic_pitch.h)
// FM SYSEX parameters 2 and 3 pick a bank and a voice in it (banks 0…2 are Plaits' built-in ones).
inline constexpr int kFmBankParam = 2;
inline constexpr int kFmVoiceParam = 3;
inline constexpr int kFmFactoryBanks = 3;
inline constexpr int kFmMaxBanks = 32;
inline constexpr int fm_bank_index(int value) { return value < 0 ? 0 : (value > 255 ? 255 : value) / 8; }
inline constexpr int fm_voice_index(int value) { return value < 0 ? 0 : (value > 255 ? 255 : value) / 8; }
inline constexpr int fm_index_value(int index) { return (index < 0 ? 0 : index > 31 ? 31 : index) * 8 + 4; }
// The built-in bank and voice of FM SYSEX's named sound `variation` (0…15); {0, 0} otherwise.
std::array<int, 2> fm_preset_voice(int variation);
// Copies built-in voice `voice` (0…31) of built-in bank `bank` (0…2), 128 packed bytes, into `out`;
// out-of-range indices are clamped.
void fm_builtin_voice(int bank, int voice, uint8_t* out);
inline constexpr bool is_vintage_model(int model) {
    return model >= kVintageFirstModel && model < kVintageFirstModel + kVintageModelCount;
}
int model_count();
const char* model_name(int model);
bool synth_needs_strike(int model);
// UI traits describe the controls actually consumed by each renderer. Invalid models/indices
// have zero controls / an empty label; URI parameter packing remains unchanged.
int macro_count(int model);
const char* macro_name(int model,int index);
std::string preset_name(int code);
int custom_code(int model,int a,int b,int c);
std::string custom_uri(int model,int a,int b,int c);
int uri_code(const char* path);
bool valid_code(int code);
std::string uri(int code);
class Synth {
public:
 Synth(); ~Synth();
 Synth(const Synth&)=delete; Synth& operator=(const Synth&)=delete;
 // Call with processing stopped, before a fresh project or offline render.
 void reset();
 void start(int code);
 // Audio thread only: updates never allocate and same-model edits retain oscillator/envelope state.
 void update(int code,const SynthParams& params);
 // FM SYSEX's voice (128 packed bytes, fm_sysex.h) or nullptr for the built-in banks. Audio thread;
 // copies the bytes, so the caller's buffer may change afterwards.
 void set_fm_voice(const uint8_t* packed);
 // FROGGY: the instrument's packed lyric (sing_text.h, 385 bytes, copied) or nullptr for the built-in one,
 // and the number of the note being started (the engine counts notes per instrument).
 void set_lyric(const uint8_t* packed);
 void set_note_index(uint32_t index);
 // SP FREEZE's source PCM, lent for one rendering section. The voice reads it only while the loan
 // lives; the destructor ends it on every way out of the scope (early returns included). The engine
 // opens a loan only inside a piece it mixes under the sample-edit lock.
 class SourceLoan {
 public:
  SourceLoan(Synth& synth,const float* data,int length,uint32_t generation,int sampleRate);
  ~SourceLoan();
  SourceLoan(const SourceLoan&)=delete; SourceLoan& operator=(const SourceLoan&)=delete;
 private:
  Synth& synth_;
 };
 int code() const { return code_; }
 float next(float hz, float sampleRate);
 bool active() const { return code_>=0; }
 // A struck model (synth_needs_strike) that has been below -80 dBFS for kDecayedSeconds: it cannot
 // sound again without a new note, so the host may fade the voice out and stop rendering it.
 static constexpr float kDecayedSeconds = 1.5f;
 bool decayed() const;
private:
 struct State; std::unique_ptr<State> s_;
 int code_=-1;
};
}
