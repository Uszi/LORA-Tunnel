// SPDX-License-Identifier: MIT
// Original bounded parser for public MeshCore wire facts. No upstream parser
// implementation or generated code is included. Protocol references:
// https://github.com/meshcore-dev/MeshCore/blob/81dbae592ffad40b4212bc0b37e82b38da08f4bc/docs/packet_format.md
// https://github.com/meshcore-dev/MeshCore/blob/81dbae592ffad40b4212bc0b37e82b38da08f4bc/docs/payloads.md
// Signature message and limits checked against Mesh.cpp and MeshCore.h at the
// same revision. This decoder observes packets; it never joins or transmits.
#include "mesh_decode.h"
#include "text_sanitize.h"
#include <cstring>

namespace tunnel { namespace mesh {
namespace {
constexpr std::size_t MaxFrame = 255;
constexpr std::size_t MaxPath = 64;
constexpr std::size_t MaxPayload = 184;
constexpr std::size_t AdvertPrefix = 100; // key32 + time4 + signature64
constexpr std::size_t MaxAdvertData = 32;

std::uint32_t little32(const std::uint8_t* p) {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) |
           (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}

std::int32_t signedLittle32(const std::uint8_t* p) {
    const std::uint32_t value = little32(p);
    return value <= 0x7fffffffU ? static_cast<std::int32_t>(value) :
        static_cast<std::int32_t>(std::int64_t(value) - 0x100000000LL);
}

bool cipherEnvelope(std::size_t length, std::size_t prefix) {
    // Current encrypted envelopes have a two-byte MAC in their prefix and
    // at least one complete AES block. This checks shape, not authenticity.
    return length >= prefix + 16 && (length - prefix) % 16 == 0;
}
} // namespace

void decodeMeshCore(const std::uint8_t* data, std::size_t length,
                    const Crypto& crypto, Result& out) {
    out = Result{};
    out.protocol = Protocol::MeshCore;
    out.status = Status::Malformed;
    if (!data || length < 2 || length > MaxFrame) return;

    out.packet_type = (data[0] >> 2) & 0x0f;
    if ((data[0] >> 6) != 0) {
        out.status = Status::Unsupported;
        return;
    }
    const std::uint8_t route = data[0] & 3;
    const std::size_t pathOffset = (route == 0 || route == 3) ? 5 : 1;
    if (length <= pathOffset) return;
    const std::uint8_t encodedPath = data[pathOffset];
    const std::uint8_t pathMode = encodedPath >> 6;
    out.path_count = encodedPath & 63;
    if (pathMode == 3) {
        out.status = Status::Unsupported;
        return;
    }
    out.path_hash_size = pathMode + 1;
    const std::size_t pathBytes = std::size_t(out.path_count) * out.path_hash_size;
    const std::size_t payloadOffset = pathOffset + 1 + pathBytes;
    if (pathBytes > MaxPath || payloadOffset >= length) return;
    const std::size_t payloadLength = length - payloadOffset;
    if (payloadLength > MaxPayload) return;
    const std::uint8_t* payload = data + payloadOffset;

    if (out.packet_type != 4) {
        switch (out.packet_type) {
        case 0: case 1: case 2: case 8: // destination/source hashes + MAC
            if (cipherEnvelope(payloadLength, 4)) out.status = Status::Encrypted;
            break;
        case 5: case 6: // channel hash + MAC
            if (cipherEnvelope(payloadLength, 3)) out.status = Status::Encrypted;
            break;
        case 7: // destination hash + ephemeral public key + MAC
            if (cipherEnvelope(payloadLength, 35)) out.status = Status::Encrypted;
            break;
        case 3:
            if (payloadLength == 4) out.status = Status::Decoded;
            break;
        default:
            out.status = Status::Unsupported;
            break;
        }
        return;
    }

    if (payloadLength < AdvertPrefix) return;
    out.signature_present = true;
    out.authentication = Authentication::Unverified;
    const std::size_t appLength = payloadLength - AdvertPrefix;
    if (appLength > MaxAdvertData) {
        // Do not verify a truncated prefix and then display unsigned suffixes.
        out.status = Status::Unsupported;
        return;
    }
    const std::uint8_t* app = payload + AdvertPrefix;
    // Validate all optional fields before exposing any node/position data.
    std::size_t nameOffset = 0;
    std::uint8_t flags = 0;
    std::int32_t latitude = 0, longitude = 0;
    if (appLength != 0) {
        flags = app[0];
        nameOffset = 1;
        if (flags & 0x10) {
            if (appLength - nameOffset < 8) return;
            latitude = signedLittle32(app + nameOffset);
            longitude = signedLittle32(app + nameOffset + 4);
            nameOffset += 8;
            if (latitude < -90000000 || latitude > 90000000 ||
                longitude < -180000000 || longitude > 180000000) return;
        }
        const std::size_t featureBytes = ((flags & 0x20) ? 2 : 0) +
                                         ((flags & 0x40) ? 2 : 0);
        if (appLength - nameOffset < featureBytes) return;
        nameOffset += featureBytes;
        if (!(flags & 0x80) && nameOffset != appLength) {
            out.status = Status::Unsupported;
            return;
        }
        if ((flags & 0x80) && nameOffset == appLength) return;
    }

    std::memcpy(out.public_key, payload, sizeof(out.public_key));
    out.has_node = true;
    out.timestamp = little32(payload + 32);
    out.has_time = true;
    out.role = flags & 0x0f;
    out.has_role = appLength != 0;
    if (flags & 0x10) {
        out.has_position = true;
        // MeshCore uses 1e6; the common observation representation uses 1e7.
        out.latitude_i = latitude * 10;
        out.longitude_i = longitude * 10;
    }
    if (flags & 0x80)
        out.sanitized = copyUtf8Bounded(out.name, sizeof(out.name),
                                        app + nameOffset, appLength - nameOffset);

    if (crypto.verify) {
        // Ed25519 signs the key, timestamp and exact app bytes. It excludes
        // the signature itself and the mutable radio route/path fields.
        std::uint8_t message[36 + MaxAdvertData];
        std::memcpy(message, payload, 36);
        if (appLength) std::memcpy(message + 36, app, appLength);
        out.authentication = crypto.verify(out.public_key, payload + 36,
                                             message, 36 + appLength) ?
            Authentication::Verified : Authentication::Failed;
    }
    // Parsing and authentication are distinct. Callers must retain Failed or
    // Unverified status alongside the reported fields, including in the UI.
    out.status = Status::Decoded;
}
} } // namespace tunnel::mesh
