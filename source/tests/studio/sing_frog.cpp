// FROGGY (model 125, Studio > Synth > Vintage): the text front end (spelling to syllables), the voice (vowel
// formants, consonants, coda), the lyric riding on the notes through studio::Synth, and the guard rails.
#include "sing_frog.h"
#include "kiss_fftr.h"
#include "synth.h"
#include "synth_params.h"
#include "mic_pitch.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <string>
#include <vector>

using namespace studio;

namespace {

constexpr float kRate = 48000.f;
constexpr int kFft = 8192;


std::vector<double> spectrum(const std::vector<float>& x, size_t from = 0) {
    kiss_fftr_cfg cfg = kiss_fftr_alloc(kFft, 0, nullptr, nullptr);
    std::vector<float> w(kFft);
    for (int i = 0; i < kFft; ++i) w[size_t(i)] = x[from + size_t(i)] * (.5f - .5f * std::cos(6.2831853f * i / (kFft - 1)));
    std::vector<kiss_fft_cpx> bins(kFft / 2 + 1);
    kiss_fftr(cfg, w.data(), bins.data());
    kiss_fftr_free(cfg);
    std::vector<double> p(bins.size());
    for (size_t k = 0; k < bins.size(); ++k) p[k] = double(bins[k].r) * bins[k].r + double(bins[k].i) * bins[k].i;
    return p;
}
int bin_of(float hz) { return int(std::lround(hz * kFft / kRate)); }
double range(const std::vector<double>& p, float lo, float hi) {
    double e = 0;
    for (int k = std::max(0, bin_of(lo)); k <= std::min(int(p.size()) - 1, bin_of(hi)); ++k) e += p[size_t(k)];
    return e;
}
double harmonic(const std::vector<double>& p, float hz, int n) { return range(p, hz * n - 12.f, hz * n + 12.f); }
double db(double powerRatio) { return 10 * std::log10(std::max(powerRatio, 1e-30)); }
double rms(const std::vector<float>& x) { double e = 0; for (float v : x) e += double(v) * v; return std::sqrt(e / x.size()); }
double peak(const std::vector<float>& x) { double m = 0; for (float v : x) m = std::max(m, double(std::fabs(v))); return m; }
bool near(double a, double b, double tolerance) { return std::fabs(a - b) <= tolerance * std::fabs(b); }


double centroid(const std::vector<double>& p) {
    double s = 0, e = 0;
    for (size_t k = 1; k < p.size(); ++k) { s += p[k] * k; e += p[k]; }
    return s / e * kRate / kFft;
}


using sing::Syllable;
Syllable syllable(const char* text, int index = 0) {
    uint8_t packed[sing::kPackedSize];
    const int n = sing::pack_lyric(text, packed);
    assert(index < n);
    Syllable s;
    std::memcpy(&s, packed + 1 + index * sing::kSyllableBytes, sizeof s);
    return s;
}
std::vector<float> sing_syllable(const Syllable& syl, const SingFrog::Settings& set, float hz, int frames, float mouth = .5f,
                                 float breath = 0.f, float throat = .5f) {
    SingFrog v;
    v.start(syl, set);
    std::vector<float> out;
    float block[24];
    for (int i = 0; i < frames; i += 24) {
        v.render(block, 24, hz, mouth, breath, throat, set);
        for (float x : block) { assert(std::isfinite(x)); out.push_back(x); }
    }
    return out;
}
SingFrog::Settings plain(int voice = 1) {
    SingFrog::Settings s; s.voice = voice; s.croak = 0.f; s.vibrato = 0.f; s.glideMs = 20.f; s.codaMs = 0.f;
    return s;
}

}  // namespace

int main() {
    // Registration.
    assert(kSingFrogModel == kFrogVoiceModel + 1 && is_vintage_model(kSingFrogModel));
    assert(model_count() == kVintageFirstModel + kVintageModelCount && model_count() == 128);
    assert(std::string(model_name(kSingFrogModel)) == "FROGGY" && !synth_needs_strike(kSingFrogModel));
    assert(macro_count(kSingFrogModel) == 3 && std::string(macro_name(kSingFrogModel, 0)) == "Mouth" &&
           std::string(macro_name(kSingFrogModel, 1)) == "Breath" && std::string(macro_name(kSingFrogModel, 2)) == "Throat");
    assert(synth_param_count(kSingFrogModel) == 10 && std::string(synth_param_name(kSingFrogModel, 2)) == "Lyrics" &&
           std::string(synth_param_name(kSingFrogModel, 8)) == "Syllable");
    assert(synth_param_value(kSingFrogModel, 3, 32) == "Frog" && synth_param_value(kSingFrogModel, 3, 224) == "Child" &&
           synth_param_value(kSingFrogModel, 7, 0) == "Off" && synth_param_value(kSingFrogModel, 8, 0) == "Auto" &&
           synth_param_value(kSingFrogModel, 8, 10) == "No. 2");
    assert(synth_param_adjust(kSingFrogModel, 8, 0, 1, false) == 5 && synth_param_adjust(kSingFrogModel, 8, 0, -1, false) == 0 &&
           synth_param_adjust(kSingFrogModel, 3, 32, 1, false) == 96);
    assert(preset_name(preset_code(kSingFrogModel, 0)) == "FROGGY / Pond Choir");
    for (int v = 0; v < 16; ++v) assert(valid_code(preset_code(kSingFrogModel, v)));

    // Text front end: words become syllables the way they are spelled.
    {
        uint8_t p[sing::kPackedSize];
        assert(sing::pack_lyric("", p) == 0 && p[0] == 0);
        assert(sing::pack_lyric("ribbit", p) == 2);
        assert(sing::pack_lyric("la la la", p) == 3);
        assert(sing::pack_lyric("Ribbit, RIBBIT - quack!", p) == 5);
        assert(sing::pack_lyric("hmm 123 ... ", p) == 0);   // nothing to sing
        std::string many;
        for (int i = 0; i < 100; ++i) many += "la ";
        assert(sing::pack_lyric(many.c_str(), p) <= sing::kMaxSyllables && p[0] <= sing::kMaxSyllables);
        const Syllable la = syllable("la");
        assert(la.onset[0] == sing::C_L && la.onset[1] == 0 && la.vowel == sing::V_AE && la.coda[0] == 0);
        const Syllable ree = syllable("ree");
        assert(ree.onset[0] == sing::C_R && ree.vowel == sing::V_I && ree.coda[0] == 0);
        const Syllable cat = syllable("cat");
        assert(cat.onset[0] == sing::C_K && cat.vowel == sing::V_AE && cat.coda[0] == sing::C_T);
        const Syllable rib = syllable("ribbit", 0), bit = syllable("ribbit", 1);
        assert(rib.onset[0] == sing::C_R && rib.coda[0] == sing::C_B && bit.onset[0] == sing::C_B && bit.coda[0] == sing::C_T);
        const Syllable shy = syllable("shine");   // a diphthong keeps both vowels
        assert(shy.onset[0] == sing::C_SH && shy.vowel == sing::V_AA && shy.vowel2 == sing::V_IH);
        const Syllable street = syllable("street");
        assert(street.onset[0] == sing::C_S && street.onset[1] == sing::C_T && street.onset[2] == sing::C_R && street.vowel == sing::V_I);
        // The same text always packs to the same bytes.
        uint8_t q[sing::kPackedSize];
        sing::pack_lyric("sing me a song", p);
        sing::pack_lyric("sing me a song", q);
        assert(std::memcmp(p, q, sizeof p) == 0);
        // Short words whose only vowel is a final e are sung, not lost (found by review): "Do Re Mi", "me", "the".
        assert(sing::pack_lyric("do re mi fa so la ti do", p) == 8);
        for (const char* w : {"me", "he", "we", "be", "she", "the", "re", "red", "bed", "yes", "I", "you", "of", "are", "go", "no"})
            assert(sing::pack_lyric(w, p) == 1);
        assert(sing::pack_lyric("row row row your boat gently down the stream", p) == 10);
        // UTF-8: a cut never splits a character (a half character would make the project file unreadable)
        {
            const std::string s = std::string(119, 'a') + "\xC3\xBC" + "xyz";   // u-umlaut straddles byte 120
            const std::string cut = sing::utf8_cut(s, 120);
            assert(cut.size() == 119 && sing::utf8_cut("abc", 120) == "abc" && sing::utf8_cut(std::string(10, 'a'), 4).size() == 4);
        }
        // The karaoke display: words with their syllable counts and first syllables.
        sing::WordSpan sp[8];
        assert(sing::lyric_words("ribbit quack, la", sp, 8) == 3);
        assert(sp[0].syllables == 2 && sp[0].firstSyllable == 0 && sp[1].syllables == 1 && sp[1].firstSyllable == 2 &&
               sp[2].syllables == 1 && sp[2].firstSyllable == 3 && sp[1].start == 7 && sp[1].length == 5);
        assert(sing::lyric_words("", sp, 8) == 0 && sing::lyric_words("hmm ... 12", sp, 8) == 0);
        std::printf("text front end ok\n");
    }

    // Vowels: the formants land where the table says (energy ratios of broad bands, 196 Hz, held 0.4 s).
    {
        auto band = [&](const std::vector<float>& x, float lo, float hi) { return range(spectrum(x, 4000), lo, hi); };
        const auto ee = sing_syllable(syllable("ee"), plain(), 196.f, 24000);     // F1 342, F2 2322
        const auto aw = sing_syllable(syllable("aw"), plain(), 196.f, 24000);     // F1 652, F2 997
        const auto oo = sing_syllable(syllable("oo"), plain(), 196.f, 24000);     // F1 378, F2 997
        const double eeHigh = band(ee, 2000.f, 2700.f), eeMid = band(ee, 1000.f, 1500.f);
        const double awHigh = band(aw, 2000.f, 2700.f), awMid = band(aw, 800.f, 1200.f);
        std::printf("ee: F2 band %.1f dB over the 1-1.5 kHz band; aw: 0.8-1.2 kHz %.1f dB over the F2 band of ee\n",
                    db(eeHigh / eeMid), db(awMid / awHigh));
        assert(eeHigh > 2.0 * eeMid);
        assert(awMid > 2.0 * awHigh);
        // aw opens the jaw more than oo: more energy around 650 Hz.
        assert(band(aw, 550.f, 750.f) > 1.5 * band(oo, 550.f, 750.f));
        // The mouth macro opens the jaw: F1 of "ee" moves up with it.
        const auto closed = sing_syllable(syllable("ee"), plain(), 196.f, 24000, 0.f), open = sing_syllable(syllable("ee"), plain(), 196.f, 24000, 1.f);
        assert(band(open, 400.f, 520.f) > 1.5 * band(closed, 400.f, 520.f));
        // A frog is darker than a child on the same vowel.
        const auto frogEe = sing_syllable(syllable("ee"), plain(0), 196.f, 24000), childEe = sing_syllable(syllable("ee"), plain(3), 196.f, 24000);
        assert(centroid(spectrum(childEe, 4000)) > 1.15 * centroid(spectrum(frogEe, 4000)));
    }

    // Consonants: an s hisses high, a plain vowel does not; a stop has a gap; a nasal sounds low.
    {
        const auto s = sing_syllable(syllable("sa"), plain(), 196.f, 9600);        // first 0.2 s: the hiss
        const auto a = sing_syllable(syllable("a"), plain(), 196.f, 9600);
        const double hiss = range(spectrum(s, 0), 5000.f, 10000.f), vowel = range(spectrum(a, 0), 5000.f, 10000.f);
        std::printf("s onset: 5-10 kHz %.1f dB over a plain vowel\n", db(hiss / vowel));
        assert(hiss > 30.0 * vowel);
        const auto pa = sing_syllable(syllable("pa"), plain(), 196.f, 4800);       // 0.1 s: closure first
        double early = 0, late = 0;
        for (size_t i = 0; i < 480; ++i) early += std::fabs(pa[i]);
        for (size_t i = 3600; i < 4080; ++i) late += std::fabs(pa[i]);
        std::printf("p: closure level %.3f, vowel level %.3f\n", early / 480, late / 480);
        assert(early < .2 * late);
    }

    // The coda arrives after its delay, and the word ends; with the coda off the vowel is held.
    {
        SingFrog::Settings withCoda = plain(); withCoda.codaMs = 600.f;
        const Syllable cat = syllable("cat");
        const auto held = sing_syllable(cat, plain(), 196.f, 96000);
        const auto word = sing_syllable(cat, withCoda, 196.f, 96000);
        double heldLate = 0, wordLate = 0, wordMid = 0;
        for (size_t i = 72000; i < 90000; ++i) { heldLate += std::fabs(held[i]); wordLate += std::fabs(word[i]); }
        for (size_t i = 14400; i < 24000; ++i) wordMid += std::fabs(word[i]);
        std::printf("coda off: late level %.4f, coda on: late level %.4f (mid %.4f)\n", heldLate / 18000, wordLate / 18000, wordMid / 9600);
        assert(heldLate / 18000 > .02);
        assert(wordLate / 18000 < .02 * (wordMid / 9600));
        SingFrog probe; probe.start(cat, withCoda);
        assert(probe.coda_start() >= uint32_t(.6f * 48000) - 2 && probe.plan_end() > probe.coda_start());
    }

    // The lyric rides on the notes through the synth: note n sings syllable n.
    {
        uint8_t packed[sing::kPackedSize];
        sing::pack_lyric("ee aw", packed);
        auto note = [&](uint32_t index, int fixed) {
            Synth s;
            const int code = custom_code(kSingFrogModel, 128, 0, 128);
            auto params = default_synth_params();
            params[kSingVoiceParam] = 96;   // man
            params[4] = 0; params[5] = 0; params[7] = 0; params[kSingSyllableParam] = fixed;
            s.start(code);
            s.set_lyric(packed);
            s.set_note_index(index);
            s.update(code, params);
            std::vector<float> x;
            for (int i = 0; i < 24000; ++i) { const float y = s.next(196.f, kRate); assert(std::isfinite(y)); x.push_back(y); }
            return x;
        };
        auto isEe = [&](const std::vector<float>& x) { const auto p = spectrum(x, 4000); return range(p, 2000.f, 2700.f) > range(p, 800.f, 1200.f); };
        assert(isEe(note(0, 0)) && !isEe(note(1, 0)) && isEe(note(2, 0)) && !isEe(note(3, 0)));   // auto: 0 1 0 1
        assert(!isEe(note(0, 10)) && isEe(note(1, 5)));   // fixed: No. 2 (aw) and No. 1 (ee) whatever the note
        // no lyric given: the built-in one sings
        Synth s;
        const int code = custom_code(kSingFrogModel, 128, 0, 128);
        s.start(code); s.update(code, default_synth_params());
        double e = 0;
        for (int i = 0; i < 24000; ++i) { const float y = s.next(196.f, kRate); e += double(y) * y; }
        assert(std::sqrt(e / 24000) > .01);
        std::printf("lyric follows the notes\n");
    }

    // Pitch source Mic: the voice sings the pitch the microphone hears, not the note; Note ignores the microphone.
    {
        assert(std::string(synth_param_name(kSingFrogModel, kSingPitchParam)) == "Pitch source" &&
               synth_param_value(kSingFrogModel, kSingPitchParam, 0) == "Note" && synth_param_value(kSingFrogModel, kSingPitchParam, 192) == "Mic" &&
               synth_param_adjust(kSingFrogModel, kSingPitchParam, 0, 1, false) == 192 && synth_param_adjust(kSingFrogModel, kSingPitchParam, 192, -1, false) == 64);
        auto render = [&](int source, float micHz) {
            studio::mic_pitch_publish(micHz);
            Synth s;
            const int code = custom_code(kSingFrogModel, 128, 0, 128);
            auto params = default_synth_params();
            params[kSingVoiceParam] = 96; params[4] = 0; params[5] = 0; params[7] = 0; params[kSingPitchParam] = source;
            s.start(code);
            s.update(code, params);
            std::vector<float> x;
            for (int i = 0; i < 24000; ++i) x.push_back(s.next(110.f, kRate));
            studio::mic_pitch_publish(0.f);
            return x;
        };
        const auto micOn = render(192, 330.f), micOff = render(0, 330.f);
        auto line = [&](const std::vector<float>& x, float hz) { return range(spectrum(x, 4000), hz * .96f, hz * 1.04f); };
        std::printf("mic on: 330 Hz line %.1f dB over 110 Hz; mic off: 110 Hz line %.1f dB over 165 Hz\n", db(line(micOn, 330.f) / line(micOn, 110.f)), db(line(micOff, 110.f) / line(micOff, 165.f)));
        assert(line(micOn, 330.f) > 4.0 * line(micOn, 110.f));
        assert(line(micOff, 110.f) > 20.0 * line(micOff, 165.f));
    }

    // Every named singer, every voice, extreme settings: finite and bounded.
    {
        for (int v = 0; v < 16; ++v) {
            Synth s;
            const int code = preset_code(kSingFrogModel, v);
            s.start(code);
            double e = 0, m = 0;
            for (int i = 0; i < 48000; ++i) { const float y = s.next(110.f, kRate); assert(std::isfinite(y)); e += double(y) * y; m = std::max(m, double(std::fabs(y))); }
            assert(m < 1.0 && std::sqrt(e / 48000) > .003);
        }
        int cases = 0;
        const char* words[] = {"ribbit", "strengths", "ah", "mmm", "sing me a song"};
        for (const char* w : words)
            for (int voice = 0; voice < 4; ++voice)
                for (float croak : {0.f, 1.f})
                    for (float glide : {8.f, 128.f})
                        for (float coda : {0.f, 150.f, 2400.f})
                            for (float breath : {0.f, 1.f})
                                for (float throat : {0.f, 1.f}) {
                                    SingFrog::Settings set; set.voice = voice; set.croak = croak; set.vibrato = 1.f; set.glideMs = glide; set.codaMs = coda;
                                    uint8_t p[sing::kPackedSize];
                                    const int n = sing::pack_lyric(w, p);
                                    for (int k = 0; k < std::max(n, 1); ++k) {
                                        Syllable syl{};
                                        if (n > 0) std::memcpy(&syl, p + 1 + k * sing::kSyllableBytes, sizeof syl);
                                        for (float hz : {30.f, 196.f, 1500.f}) assert(peak(sing_syllable(syl, set, hz, 9600, 1.f, breath, throat)) < 1.0);
                                        ++cases;
                                    }
                                }
        std::printf("%d extreme settings x 3 pitches: finite and below 1.0\n", cases);
    }
    std::printf("SING_FROG_PASS\n");
}
