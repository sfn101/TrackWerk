// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <cstdio>

namespace pt {
// Check in unsigned wide arithmetic BEFORE narrowing a RIFF size to long. Windows and
// armhf have 32-bit long; a forged size must never become a backwards seek there.
inline bool riff_body_end(long position, uint32_t bytes, long limit, long& end,
                          bool padded = true) {
    if (position < 0 || limit < position) return false;
    const uint64_t span = uint64_t(bytes) + (padded ? (bytes & 1u) : 0u);
    if (span > uint64_t(limit - position)) return false;
    end = position + static_cast<long>(span);
    return true;
}

inline bool seekable_file_end(FILE* file, long& end) {
    const long position = std::ftell(file);
    if (position < 0 || std::fseek(file, 0, SEEK_END) != 0) return false;
    end = std::ftell(file);
    return end >= position && std::fseek(file, position, SEEK_SET) == 0;
}
}  // namespace pt
