#pragma once
#include <algorithm>
#include <cstddef>
#include <cstring>

// Host fixtures must not redefine libc's strlcpy, including fortified versions.
// Include the system declarations first, then redirect only fixture translation units.
inline std::size_t loraTunnelHostStrlcpy(char* out, const char* source, std::size_t capacity) {
    const std::size_t length = std::strlen(source);
    if (capacity) {
        const std::size_t copied = std::min(length, capacity - 1);
        std::memcpy(out, source, copied);
        out[copied] = '\0';
    }
    return length;
}

#ifdef strlcpy
#undef strlcpy
#endif
#define strlcpy loraTunnelHostStrlcpy
