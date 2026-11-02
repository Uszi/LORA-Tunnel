#pragma once

#include <cmath>
#include <cstdint>

namespace tunnel {

// Validate before narrowing SD JSON integers into hardware-sized fields.
inline bool validLoRaSettings(double frequency, double bandwidth,
                              std::int64_t sf, std::int64_t cr,
                              std::int64_t preamble) noexcept {
    return std::isfinite(frequency) && frequency >= 863.0 && frequency <= 928.0 &&
           (bandwidth == 125.0 || bandwidth == 250.0 || bandwidth == 500.0) &&
           sf >= 7 && sf <= 12 && cr >= 5 && cr <= 8 &&
           preamble >= 8 && preamble <= 64;
}

}  // namespace tunnel
