#pragma once

#include <cstddef>
#include <cstdint>

namespace lt {

// Values extracted from a possible Meshtastic radio header, not an authenticated
// identity or proof of protocol. See docs/LORA.md for the classification contract.
struct MeshHeader {
    std::uint32_t to = 0;
    std::uint32_t from = 0;
    std::uint32_t id = 0;
    std::uint8_t channel = 0;
    std::uint8_t hop_limit = 0;
    std::uint8_t hop_start = 0;
    std::uint8_t next_hop = 0;
    std::uint8_t relay_node = 0;
    bool want_ack = false;
    bool via_mqtt = false;
};

static constexpr std::size_t kMeshHeaderBytes = 16;
static constexpr std::size_t kMaxLoRaPacketBytes = 255;

// Parse a header candidate from a complete received LoRa packet. Success means
// only that the bounded buffer contains a readable header layout. The caller
// must also retain radio settings, CRC/read status, raw bytes and classification
// confidence. This function intentionally makes no payload/encryption claims.
// Failure leaves the output unchanged. No allocation or unaligned access occurs.
inline bool parseMeshHeader(const std::uint8_t* bytes, std::size_t length,
                            MeshHeader& output) noexcept {
    if (bytes == nullptr || length < kMeshHeaderBytes || length > kMaxLoRaPacketBytes) {
        return false;
    }

    const auto read32 = [](const std::uint8_t* p) noexcept {
        return static_cast<std::uint32_t>(p[0]) |
               (static_cast<std::uint32_t>(p[1]) << 8U) |
               (static_cast<std::uint32_t>(p[2]) << 16U) |
               (static_cast<std::uint32_t>(p[3]) << 24U);
    };

    MeshHeader candidate{};
    candidate.to = read32(bytes);
    candidate.from = read32(bytes + 4);
    candidate.id = read32(bytes + 8);
    const std::uint8_t flags = bytes[12];
    candidate.hop_limit = flags & 0x07U;
    candidate.want_ack = (flags & 0x08U) != 0;
    candidate.via_mqtt = (flags & 0x10U) != 0;
    candidate.hop_start = (flags >> 5U) & 0x07U;
    candidate.channel = bytes[13];
    candidate.next_hop = bytes[14];
    candidate.relay_node = bytes[15];
    output = candidate;
    return true;
}

}  // namespace lt
