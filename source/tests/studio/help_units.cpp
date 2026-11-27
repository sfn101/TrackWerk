// Real units in help lines (specs/help-and-lessons.md, phase B). Every "XX=-6 dB" or "XX is -6 dB" in a
// help line is recomputed from the code's own conversion (songcore::hex_to_float, a level of XX/255)
// and must match to the precision printed. Also counts the help topics that get the blank row under
// their title. Portable, no assert: CI runs it on Linux and Windows. Usage: pthelp-units-check
#include "songcore/model.h"
#include "ui/fx_helper.h"
#include "ui/help_text.h"
#include "ui/modules/help_overlay.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {
int failures = 0, figures = 0;
void check(bool ok, const std::string& what) {
    if (!ok) { std::printf("FAIL: %s\n", what.c_str()); ++failures; }
}

int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Every "<hex><sep><number> dB" in `line`, where <sep> is "=" or " is ".
void check_line(const std::string& line) {
    for (size_t at = line.find(" dB"); at != std::string::npos; at = line.find(" dB", at + 3)) {
        size_t start = at;
        while (start > 0 && (std::isdigit(static_cast<unsigned char>(line[start - 1])) || line[start - 1] == '.' ||
                             line[start - 1] == '-'))
            --start;
        const std::string number = line.substr(start, at - start);
        size_t hexEnd = start;
        if (hexEnd >= 1 && line[hexEnd - 1] == '=') hexEnd -= 1;
        else if (hexEnd >= 4 && line.compare(hexEnd - 4, 4, " is ") == 0) hexEnd -= 4;
        else continue;   // a dB figure that does not name a hex value
        if (hexEnd < 2 || number.empty()) continue;
        const int hi = hex_digit(line[hexEnd - 2]), lo = hex_digit(line[hexEnd - 1]);
        if (hi < 0 || lo < 0) continue;
        const int hex = hi * 16 + lo;
        const double printed = std::stod(number);
        const double actual = 20.0 * std::log10(songcore::hex_to_float(hex));
        const size_t dot = number.find('.');
        const double tolerance = dot == std::string::npos ? 0.5 : 0.05;
        ++figures;
        check(std::fabs(printed - actual) < tolerance,
              "\"" + line + "\": " + number + " dB, but " + std::to_string(actual) + " dB in the code");
    }
}
}  // namespace

int main() {
    using namespace pt::ui;
    for (const auto& d : effect_descriptions())
        for (const auto& line : d) check_line(line);
    for (const HelpEntry& e : HELP_ENTRIES) {
        for (const char* l : {e.line1, e.line2, e.line3}) if (l) check_line(l);
        for (const char* l : e.body) if (l) check_line(l);
        for (const char* l : e.keys) if (l) check_line(l);
    }
    check(figures >= 5, "the dB figures were found (" + std::to_string(figures) + ")");

    int withAir = 0;
    for (const HelpEntry& e : HELP_ENTRIES) withAir += help_overlay_air(e) ? 1 : 0;
    std::printf("      %d dB figures checked; %d of %zu help topics get the blank row under the title\n", figures,
                withAir, sizeof(HELP_ENTRIES) / sizeof(HELP_ENTRIES[0]));
    check(withAir > 0, "some topics get the blank row");

    std::printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
