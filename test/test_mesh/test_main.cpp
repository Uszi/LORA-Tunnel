#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <unity.h>

#include "mesh_header.h"

void setUp() {}
void tearDown() {}

void test_explicit_little_endian_fields_and_flags() {
    // Synthetic fixture, not a captured packet. 0xBA: hop 2, ACK, MQTT, start 5.
    const std::uint8_t packet[] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0x78, 0x56, 0x34, 0x12,
        0xEF, 0xCD, 0xAB, 0x89, 0xBA, 0x08, 0xAD, 0x42,
        0x00, 0xFF
    };
    lt::MeshHeader header{};
    TEST_ASSERT_TRUE(lt::parseMeshHeader(packet, sizeof(packet), header));
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFU, header.to);
    TEST_ASSERT_EQUAL_HEX32(0x12345678U, header.from);
    TEST_ASSERT_EQUAL_HEX32(0x89ABCDEFU, header.id);
    TEST_ASSERT_EQUAL_UINT8(2, header.hop_limit);
    TEST_ASSERT_EQUAL_UINT8(5, header.hop_start);
    TEST_ASSERT_TRUE(header.want_ack);
    TEST_ASSERT_TRUE(header.via_mqtt);
    TEST_ASSERT_EQUAL_HEX8(0x08, header.channel);
    TEST_ASSERT_EQUAL_HEX8(0xAD, header.next_hop);
    TEST_ASSERT_EQUAL_HEX8(0x42, header.relay_node);
}

void test_all_truncated_headers_leave_output_unchanged() {
    const std::uint8_t bytes[lt::kMeshHeaderBytes] = {};
    for (std::size_t length = 0; length < lt::kMeshHeaderBytes; ++length) {
        lt::MeshHeader header{};
        header.from = 0xABCDEF01U;
        header.hop_limit = 6;
        header.want_ack = true;
        TEST_ASSERT_FALSE(lt::parseMeshHeader(bytes, length, header));
        TEST_ASSERT_EQUAL_HEX32(0xABCDEF01U, header.from);
        TEST_ASSERT_EQUAL_UINT8(6, header.hop_limit);
        TEST_ASSERT_TRUE(header.want_ack);
    }
}

void test_null_and_oversize_are_rejected_before_read() {
    lt::MeshHeader header{};
    header.id = 123;
    const std::uint8_t tiny[1] = {};
    TEST_ASSERT_FALSE(lt::parseMeshHeader(nullptr, 0, header));
    TEST_ASSERT_FALSE(lt::parseMeshHeader(nullptr, 16, header));
    TEST_ASSERT_FALSE(lt::parseMeshHeader(tiny, 256, header));
    TEST_ASSERT_FALSE(lt::parseMeshHeader(tiny, std::numeric_limits<std::size_t>::max(), header));
    TEST_ASSERT_EQUAL_UINT32(123, header.id);
}

void test_boundary_sizes_and_unaligned_buffer() {
    std::array<std::uint8_t, lt::kMaxLoRaPacketBytes + 1> storage{};
    storage[5] = 0xFE;
    storage[13] = 0xE7; // Max hop fields, neither ACK nor MQTT.
    lt::MeshHeader header{};
    TEST_ASSERT_TRUE(lt::parseMeshHeader(storage.data() + 1, 16, header));
    TEST_ASSERT_EQUAL_UINT32(254, header.from);
    TEST_ASSERT_EQUAL_UINT8(7, header.hop_limit);
    TEST_ASSERT_EQUAL_UINT8(7, header.hop_start);
    TEST_ASSERT_FALSE(header.want_ack);
    TEST_ASSERT_FALSE(header.via_mqtt);
    TEST_ASSERT_TRUE(lt::parseMeshHeader(storage.data() + 1, 255, header));
}

void test_arbitrary_bytes_are_only_a_layout_candidate() {
    // No magic number/signature exists in this header. Acceptance here must
    // never be used to label arbitrary bytes a verified Meshtastic device.
    const std::uint8_t bytes[16] = {};
    lt::MeshHeader header{};
    TEST_ASSERT_TRUE(lt::parseMeshHeader(bytes, sizeof(bytes), header));
    TEST_ASSERT_EQUAL_UINT32(0, header.from);
    TEST_ASSERT_EQUAL_UINT8(0, header.hop_start);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_explicit_little_endian_fields_and_flags);
    RUN_TEST(test_all_truncated_headers_leave_output_unchanged);
    RUN_TEST(test_null_and_oversize_are_rejected_before_read);
    RUN_TEST(test_boundary_sizes_and_unaligned_buffer);
    RUN_TEST(test_arbitrary_bytes_are_only_a_layout_candidate);
    return UNITY_END();
}
