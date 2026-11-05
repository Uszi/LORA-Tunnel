#pragma once
// SPDX-License-Identifier: MIT
// Original public-input checks around the pinned Ed25519 verifier. Scalar and
// coordinate bounds follow RFC8032 sections 5.1.3 / 5.1.7. Low-order encoded
// y values were checked against libsodium 1.0.18's ge25519_has_small_order:
// https://github.com/jedisct1/libsodium/blob/1.0.18/src/libsodium/crypto_core/ed25519/ref10/ed25519_ref10.c
// The numeric low-order table is included with its upstream ISC notice:
//
// Copyright (c) 2013-2019 Frank Denis <j at pureftpd dot org>
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

#include <cstddef>
#include <cstdint>

namespace tunnel { namespace mesh {
namespace signature_detail {
inline bool lessLittle32(const std::uint8_t* value, const std::uint8_t* limit,
                         bool maskSign = false) {
    // Inputs are public signature/key bytes; this is not a secret comparison.
    for (std::size_t i = 32; i != 0; --i) {
        const std::uint8_t byte = value[i - 1] & ((maskSign && i == 32) ? 0x7f : 0xff);
        if (byte != limit[i - 1]) return byte < limit[i - 1];
    }
    return false;
}

inline bool pointAllowed(const std::uint8_t* point) {
    static constexpr std::uint8_t prime[32] = {
        0xed,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
        0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0x7f
    };
    if (!lessLittle32(point, prime, true)) return false;
    // Five distinct y encodings cover all eight torsion points when the x
    // sign bit is ignored. Noncanonical aliases p and p+1 fail above.
    static constexpr std::uint8_t lowOrderY[5][32] = {
        {0},
        {1},
        {0x26,0xe8,0x95,0x8f,0xc2,0xb2,0x27,0xb0,0x45,0xc3,0xf4,0x89,0xf2,0xef,0x98,0xf0,
         0xd5,0xdf,0xac,0x05,0xd3,0xc6,0x33,0x39,0xb1,0x38,0x02,0x88,0x6d,0x53,0xfc,0x05},
        {0xc7,0x17,0x6a,0x70,0x3d,0x4d,0xd8,0x4f,0xba,0x3c,0x0b,0x76,0x0d,0x10,0x67,0x0f,
         0x2a,0x20,0x53,0xfa,0x2c,0x39,0xcc,0xc6,0x4e,0xc7,0xfd,0x77,0x92,0xac,0x03,0x7a},
        {0xec,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
         0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0x7f}
    };
    for (const auto& excluded : lowOrderY) {
        bool equal = true;
        for (std::size_t i = 0; i != 32; ++i)
            equal &= (point[i] & (i == 31 ? 0x7f : 0xff)) == excluded[i];
        if (equal) return false;
    }
    return true;
}
} // namespace signature_detail

inline bool ed25519InputsAllowed(const std::uint8_t publicKey[32],
                                const std::uint8_t signature[64]) {
    if (!publicKey || !signature) return false;
    static constexpr std::uint8_t order[32] = {
        0xed,0xd3,0xf5,0x5c,0x1a,0x63,0x12,0x58,0xd6,0x9c,0xf7,0xa2,0xde,0xf9,0xde,0x14,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0x10
    };
    return signature_detail::lessLittle32(signature + 32, order) &&
           signature_detail::pointAllowed(publicKey) &&
           signature_detail::pointAllowed(signature);
}
} } // namespace tunnel::mesh
