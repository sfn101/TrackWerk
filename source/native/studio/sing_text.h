#pragma once
// SING TEXT: typed words → sung syllables for FROGGY (sing_frog.h). Header-only, no allocation after
// the call, fixed-size output, usable on the audio-adjacent threads (the UI thread calls it when the lyric
// changes and the engine copies the packed result).
//
// The spelling-to-sound rules below are a port of the compact letter-to-sound rule set of Chimera Vocal
// Synth (crates/chimera/src/phonetics/english.rs), Copyright (c) 2026 Jameson Hodge, MIT License
// (https://github.com/jamesondh/chimera-vocal-synth); the licence text is in docs/THIRD-PARTY-CHIMERA.md.
// The vowel formant values in sing_frog.h are the published means of Hillenbrand, Getty, Clark and Wheeler
// (1995), as listed in the same project. Everything else (syllable packing, the sound engine) is our own.
//
// A syllable is   onset (up to 3 consonants) + vowel (+ a second vowel for a glide) + coda (up to 3).
// Typed text splits at spaces, hyphens and punctuation; every word is turned into phones by the rules, the
// phones are cut into syllables (a single consonant between two vowels starts the next syllable, a pair
// splits between them), and the syllables are packed 8 bytes each. Spell words the way they should sound:
// "la la ree", "ri-bbit", "kwaak".
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>

namespace studio {
namespace sing {

// Phone codes. 0 is "none".
enum Phone : uint8_t {
    P_NONE = 0,
    // vowels
    V_I = 1, V_IH, V_E, V_EH, V_AE, V_AA, V_AO, V_O, V_UH, V_U, V_AH, V_SCHWA, V_ER,
    V_LAST = V_ER,
    // consonants
    C_P = 20, C_B, C_T, C_D, C_K, C_G, C_F, C_V, C_TH, C_DH, C_S, C_Z, C_SH, C_ZH, C_H, C_CH, C_JH,
    C_M, C_N, C_NG, C_L, C_R, C_W, C_J,
    C_LAST = C_J,
};
inline constexpr bool is_vowel(uint8_t p) { return p >= V_I && p <= V_LAST; }
inline constexpr bool is_consonant(uint8_t p) { return p >= C_P && p <= C_LAST; }

inline constexpr int kMaxSyllables = 48;
inline constexpr int kSyllableBytes = 8;
inline constexpr int kMaxTextChars = 120;
// Packed layout: byte 0 = syllable count (0…48), then per syllable: onset[3], vowel, vowel2, coda[3].
inline constexpr int kPackedSize = 1 + kMaxSyllables * kSyllableBytes;

struct Syllable {
    uint8_t onset[3];
    uint8_t vowel, vowel2;
    uint8_t coda[3];
};

namespace detail {

struct Rule { const char* pre; const char* grapheme; const char* post; const char* phones; };
// (pre, grapheme, post, phones): tried in this order at each position. '#' word boundary, 'V' vowel letter,
// 'C' consonant letter. Phones are space separated tokens (see token()).
inline constexpr Rule kRules[] = {
    {"", "tion", "", "SH ER N"}, {"", "sion", "", "ZH ER N"}, {"", "ture", "", "CH ER"}, {"", "ough", "#", "O"},
    {"", "augh", "", "AO"}, {"", "igh", "", "AY"}, {"", "eigh", "", "EY"}, {"", "tch", "", "CH"}, {"", "dge", "", "JH"},
    {"", "sch", "", "S K"}, {"", "ch", "", "CH"}, {"", "sh", "", "SH"}, {"", "th", "", "TH"}, {"", "ph", "", "F"},
    {"", "wh", "", "W"}, {"", "ck", "", "K"}, {"", "ng", "#", "NG"}, {"", "ng", "", "NG G"}, {"", "qu", "", "K W"},
    {"#", "kn", "", "N"}, {"#", "wr", "", "R"}, {"", "gh", "", ""},
    {"", "ee", "", "I"}, {"", "ea", "", "I"}, {"", "oo", "", "U"}, {"", "ou", "", "AW"}, {"", "ow", "#", "O"},
    {"", "ow", "", "AW"}, {"", "oi", "", "OY"}, {"", "oy", "", "OY"}, {"", "ai", "", "EY"}, {"", "ay", "", "EY"},
    {"", "au", "", "AO"}, {"", "aw", "", "AO"}, {"", "ie", "#", "AY"}, {"", "ie", "", "I"}, {"", "ei", "", "EY"},
    {"", "ey", "#", "I"}, {"", "oa", "", "O"}, {"", "ue", "#", "U"},
    {"", "er", "#", "ER"}, {"", "er", "C", "ER"}, {"", "ir", "", "ER"}, {"", "ur", "", "ER"}, {"", "or", "", "AO R"},
    {"", "ar", "", "AA R"},
    {"", "le", "#", "SCHWA L"}, {"", "ed", "#", "D"}, {"", "es", "#", "Z"}, {"C", "s", "#", "S"}, {"V", "s", "#", "Z"},
    {"", "y", "#", "I"}, {"#", "y", "", "J"}, {"C", "y", "", "AY"}, {"", "e", "#", ""},
    {"", "a", "CV", "EY"}, {"", "a", "", "AE"}, {"", "e", "", "EH"}, {"", "i", "CV", "AY"}, {"", "i", "", "IH"},
    {"", "o", "CV", "O"}, {"", "o", "", "AA"}, {"", "u", "CV", "U"}, {"", "u", "", "AH"}, {"", "y", "", "IH"},
    {"", "c", "e", "S"}, {"", "c", "i", "S"}, {"", "c", "y", "S"}, {"", "c", "", "K"}, {"", "g", "e", "JH"},
    {"", "g", "i", "JH"}, {"", "g", "", "G"},
    {"", "x", "", "K S"}, {"", "j", "", "JH"}, {"", "q", "", "K"}, {"", "b", "", "B"}, {"", "d", "", "D"},
    {"", "f", "", "F"}, {"", "h", "", "H"}, {"", "k", "", "K"}, {"", "l", "", "L"}, {"", "m", "", "M"},
    {"", "n", "", "N"}, {"", "p", "", "P"}, {"", "r", "", "R"}, {"", "s", "", "S"}, {"", "t", "", "T"},
    {"", "v", "", "V"}, {"", "w", "", "W"}, {"", "z", "", "Z"},
};

struct Token { const char* name; uint8_t phone, glide; };
// A diphthong is a vowel and the vowel it glides to.
inline constexpr Token kTokens[] = {
    {"I", V_I, 0}, {"IH", V_IH, 0}, {"EH", V_EH, 0}, {"AE", V_AE, 0}, {"AA", V_AA, 0}, {"AO", V_AO, 0},
    {"UH", V_UH, 0}, {"U", V_U, 0}, {"AH", V_AH, 0}, {"SCHWA", V_SCHWA, 0}, {"ER", V_ER, 0},
    {"EY", V_EH, V_IH}, {"AY", V_AA, V_IH}, {"OY", V_AO, V_IH}, {"AW", V_AA, V_UH}, {"O", V_O, V_UH},
    {"P", C_P, 0}, {"B", C_B, 0}, {"T", C_T, 0}, {"D", C_D, 0}, {"K", C_K, 0}, {"G", C_G, 0}, {"F", C_F, 0},
    {"V", C_V, 0}, {"TH", C_TH, 0}, {"DH", C_DH, 0}, {"S", C_S, 0}, {"Z", C_Z, 0}, {"SH", C_SH, 0}, {"ZH", C_ZH, 0}, {"H", C_H, 0},
    {"CH", C_CH, 0}, {"JH", C_JH, 0}, {"M", C_M, 0}, {"N", C_N, 0}, {"NG", C_NG, 0}, {"L", C_L, 0}, {"R", C_R, 0},
    {"W", C_W, 0}, {"J", C_J, 0},
};

inline bool vowel_letter(char c) { return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u'; }
inline bool letter(char c) { return c >= 'a' && c <= 'z'; }

// Does context class `ctx` (a string of '#', 'V', 'C', read in `dir`) hold around the word w[0..n)?
inline bool context(const char* w, int n, int index, const char* ctx, int dir) {
    int i = index;
    for (const char* c = ctx; *c; ++c) {
        const char ch = (i >= 0 && i < n) ? w[i] : 0;
        if (*c == '#') { if (ch != 0) return false; }
        else if (*c == 'V') { if (!vowel_letter(ch)) return false; }
        else if (*c == 'C') { if (!letter(ch) || vowel_letter(ch)) return false; }
        else if (ch != *c) return false;   // a literal letter, as in ("", "c", "e")
        i += dir;
    }
    return true;
}

struct PhoneRun {
    uint8_t phone[40];
    uint8_t glide[40];
    int count = 0;
};

inline void add_tokens(PhoneRun& run, const char* tokens) {
    const char* p = tokens;
    while (*p) {
        while (*p == ' ') ++p;
        if (!*p) break;
        char name[8];
        int len = 0;
        while (*p && *p != ' ' && len < 7) name[len++] = *p++;
        name[len] = 0;
        for (const Token& t : kTokens)
            if (std::strcmp(t.name, name) == 0) {
                if (run.count < 40) { run.phone[run.count] = t.phone; run.glide[run.count] = t.glide; ++run.count; }
                break;
            }
    }
}

// Common short words the spelling rules get wrong or lose altogether (a final e is silent, so "me" would have no
// vowel at all): sung the way they are said.
struct Exception { const char* word; const char* phones; };
inline constexpr Exception kExceptions[] = {
    {"me", "M I"}, {"he", "H I"}, {"we", "W I"}, {"be", "B I"}, {"she", "SH I"}, {"the", "DH AH"}, {"re", "R EY"},
    {"ye", "J I"}, {"mi", "M I"}, {"ti", "T I"}, {"do", "D O"}, {"so", "S O"}, {"go", "G O"}, {"no", "N O"},
    {"to", "T U"}, {"i", "AY"}, {"you", "J U"}, {"your", "J AO R"}, {"of", "AH V"}, {"are", "AA R"}, {"was", "W AH Z"},
    {"red", "R EH D"}, {"bed", "B EH D"}, {"fed", "F EH D"}, {"yes", "J EH S"}, {"oh", "O"}, {"a", "AH"}, {"or", "AO R"},
};
// Spelling → phones for one lower-case word (letters only).
inline void word_to_phones(const char* w, int n, PhoneRun& run) {
    for (const Exception& e : kExceptions)
        if (int(std::strlen(e.word)) == n && std::strncmp(e.word, w, size_t(n)) == 0) { add_tokens(run, e.phones); return; }
    int i = 0;
    while (i < n) {
        bool done = false;
        for (const Rule& r : kRules) {
            const int g = int(std::strlen(r.grapheme));
            if (i + g > n || std::strncmp(w + i, r.grapheme, size_t(g)) != 0) continue;
            // The "pre" context reads backwards: its last character is the one next to the grapheme.
            const int preLen = int(std::strlen(r.pre));
            bool preOk = true;
            for (int k = 0; k < preLen && preOk; ++k) {
                const char* one = nullptr;
                char single[2] = {r.pre[preLen - 1 - k], 0};
                one = single;
                preOk = context(w, n, i - 1 - k, one, -1);
            }
            if (!preOk) continue;
            if (!context(w, n, i + g, r.post, 1)) continue;
            add_tokens(run, r.phones);
            i += g;
            done = true;
            break;
        }
        if (!done) ++i;
    }
}

}  // namespace detail

// Typed text → packed syllables (kPackedSize bytes, zero-filled first). Returns the syllable count.
inline int pack_lyric(const char* text, uint8_t* out) {
    std::memset(out, 0, size_t(kPackedSize));
    int count = 0;
    char word[32];
    int wlen = 0;
    auto flush = [&]() {
        if (wlen == 0) return;
        detail::PhoneRun run;
        detail::word_to_phones(word, wlen, run);
        wlen = 0;
        // Nuclei: the vowels. A word with none (e.g. "hm") is skipped.
        int nuclei[16];
        int n = 0;
        for (int i = 0; i < run.count && n < 16; ++i)
            if (is_vowel(run.phone[i])) nuclei[n++] = i;
        if (n == 0) return;
        int from = 0;   // first phone of the current syllable's onset
        for (int s = 0; s < n && count < kMaxSyllables; ++s) {
            const int v = nuclei[s];
            Syllable syl{};
            // Onset: everything from `from` up to the vowel (at most the last 3).
            int on = v - from;
            int onStart = from;
            if (on > 3) onStart = v - 3, on = 3;
            for (int k = 0; k < on; ++k) syl.onset[k] = run.phone[onStart + k];
            syl.vowel = run.phone[v];
            syl.vowel2 = run.glide[v];
            // Coda and where the next onset starts: between this vowel and the next one.
            int end = s + 1 < n ? nuclei[s + 1] : run.count;
            const int between = end - (v + 1);
            int codaCount, next;
            if (s + 1 >= n) { codaCount = between; next = end; }
            else if (between <= 1) { codaCount = 0; next = v + 1; }
            else { codaCount = between / 2; next = v + 1 + codaCount; }
            if (codaCount > 3) codaCount = 3;
            for (int k = 0; k < codaCount; ++k) syl.coda[k] = run.phone[v + 1 + k];
            from = next;
            uint8_t* dst = out + 1 + count * kSyllableBytes;
            std::memcpy(dst, &syl, sizeof syl);
            ++count;
        }
    };
    int chars = 0;
    for (const char* p = text; p && *p && chars < kMaxTextChars; ++p, ++chars) {
        char c = *p;
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
        if (detail::letter(c)) {
            if (wlen < 31) word[wlen++] = c;
        } else if (c == '\'') {
            continue;   // "don't" stays one word
        } else {
            flush();
        }
    }
    flush();
    out[0] = uint8_t(count);
    return count;
}

// The words of a lyric with how many syllables each has and where its first one sits in the packed list: what the
// karaoke display needs to say which word is being sung. At most `max` words; returns how many.
struct WordSpan { int start, length, firstSyllable, syllables; };
inline int lyric_words(const char* text, WordSpan* out, int max) {
    int words = 0, syllable = 0, chars = 0;
    int start = -1;
    uint8_t scratch[kPackedSize];
    auto close = [&](int end) {
        if (start < 0) return;
        char word[40];
        const int len = std::min(end - start, 39);
        std::memcpy(word, text + start, size_t(len));
        word[len] = 0;
        const int n = pack_lyric(word, scratch);
        const int fit = std::min(n, kMaxSyllables - syllable);   // pack_lyric stops at the limit, so a word may fit in part
        if (fit > 0 && words < max) out[words++] = {start, end - start, syllable, fit};
        syllable += std::max(fit, 0);
        start = -1;
    };
    for (const char* p = text; p && *p && chars < kMaxTextChars; ++p, ++chars) {
        const char c = *p;
        const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '\'';
        if (letter) { if (start < 0) start = chars; }
        else close(chars);
    }
    close(chars);
    return words;
}

// Cuts `text` to at most `maxBytes` bytes without splitting a UTF-8 character (a half character makes the project
// file unreadable). Returns the cut string.
inline std::string utf8_cut(const std::string& text, size_t maxBytes) {
    if (text.size() <= maxBytes) return text;
    size_t end = maxBytes;
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80) --end;   // back to a character start
    return text.substr(0, end);
}

}  // namespace sing
}  // namespace studio
