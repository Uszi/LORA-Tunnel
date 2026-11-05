#include <unity.h>
#include <array>
#include <cstring>
#include <limits>
#include <vector>
#include <TunnelMeshDecode.h>

using namespace tunnel::mesh;
void setUp() {}
void tearDown() {}

namespace {
using Bytes = std::vector<std::uint8_t>;
void append32(Bytes& bytes, std::uint32_t value) {
    for (unsigned i = 0; i != 4; ++i) bytes.push_back(std::uint8_t(value >> (i * 8)));
}
Bytes advert(const Bytes& app = {}, std::uint8_t route = 1, std::uint8_t path = 0) {
    Bytes bytes{std::uint8_t(0x10 | route)};
    if (route == 0 || route == 3) bytes.insert(bytes.end(), {0x34,0x12,0x78,0x56});
    bytes.push_back(path);
    bytes.insert(bytes.end(), (path & 63) * ((path >> 6) + 1), 0x6b);
    for (unsigned i = 0; i != 32; ++i) bytes.push_back(std::uint8_t(i + 1));
    append32(bytes, 0x65543210U);
    for (unsigned i = 0; i != 64; ++i) bytes.push_back(std::uint8_t(i + 0x40));
    bytes.insert(bytes.end(), app.begin(), app.end());
    return bytes;
}
Result decode(const Bytes& bytes, const Crypto& crypto = {}) {
    Result result;
    decodeMeshCore(bytes.data(), bytes.size(), crypto, result);
    return result;
}
Bytes expectedMessage;
bool verifyCalled = false;
bool verifyAccept = true;
bool inspectVerify(const std::uint8_t key[32], const std::uint8_t signature[64],
                   const std::uint8_t* message, std::size_t length) {
    verifyCalled = true;
    TEST_ASSERT_EQUAL_UINT(expectedMessage.size(), length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedMessage.data(), message, length);
    for (unsigned i = 0; i != 32; ++i) TEST_ASSERT_EQUAL_UINT8(i + 1, key[i]);
    for (unsigned i = 0; i != 64; ++i) TEST_ASSERT_EQUAL_UINT8(i + 0x40, signature[i]);
    return verifyAccept;
}
} // namespace

void test_advert_optional_fields_and_signed_coordinates() {
    // Synthetic wire fixture: repeater, position, both future feature words,
    // and a UTF-8 name. Does not assert cryptographic validity.
    Bytes app{0xf2};
    append32(app, std::uint32_t(-12345678));
    append32(app, 123456789);
    app.insert(app.end(), {0x11,0x22,0x33,0x44,'N','o','d','e',' ',0xc3,0xa9});
    const auto result = decode(advert(app));
    TEST_ASSERT_EQUAL_INT(int(Status::Decoded), int(result.status));
    TEST_ASSERT_EQUAL_INT(int(Protocol::MeshCore), int(result.protocol));
    TEST_ASSERT_EQUAL_INT(int(Authentication::Unverified), int(result.authentication));
    TEST_ASSERT_TRUE(result.signature_present);
    TEST_ASSERT_TRUE(result.has_node);
    TEST_ASSERT_TRUE(result.has_position);
    TEST_ASSERT_TRUE(result.has_time);
    TEST_ASSERT_FALSE(result.has_altitude);
    TEST_ASSERT_TRUE(result.has_role);
    TEST_ASSERT_FALSE(result.has_hardware);
    TEST_ASSERT_EQUAL_UINT32(2, result.role);
    TEST_ASSERT_EQUAL_UINT32(0x65543210U, result.timestamp);
    TEST_ASSERT_EQUAL_INT32(-123456780, result.latitude_i);
    TEST_ASSERT_EQUAL_INT32(1234567890, result.longitude_i);
    TEST_ASSERT_EQUAL_STRING("Node \xc3\xa9", result.name);
    TEST_ASSERT_FALSE(result.sanitized);
    for (unsigned i = 0; i != 32; ++i) TEST_ASSERT_EQUAL_UINT8(i + 1, result.public_key[i]);
}

void test_all_routes_and_current_path_hash_sizes() {
    for (std::uint8_t route = 0; route != 4; ++route) {
        for (std::uint8_t mode = 0; mode != 3; ++mode) {
            const auto result = decode(advert({0x81,'A'}, route, std::uint8_t((mode << 6) | 3)));
            TEST_ASSERT_EQUAL_INT(int(Status::Decoded), int(result.status));
            TEST_ASSERT_EQUAL_UINT8(3, result.path_count);
            TEST_ASSERT_EQUAL_UINT8(mode + 1, result.path_hash_size);
            TEST_ASSERT_EQUAL_STRING("A", result.name);
        }
    }
}

void test_signature_adapter_receives_exact_canonical_message() {
    const Bytes app{0x81,'A',0,'B'};
    auto bytes = advert(app, 3, 0x82);
    const std::size_t payload = 12; // header + transport4 + pathlen + path6
    expectedMessage.assign(bytes.begin() + payload, bytes.begin() + payload + 36);
    expectedMessage.insert(expectedMessage.end(), app.begin(), app.end());
    Crypto crypto;
    crypto.verify = inspectVerify;
    verifyCalled = false;
    verifyAccept = true;
    auto result = decode(bytes, crypto);
    TEST_ASSERT_TRUE(verifyCalled);
    TEST_ASSERT_EQUAL_INT(int(Authentication::Verified), int(result.authentication));
    TEST_ASSERT_EQUAL_STRING("A?B", result.name);
    TEST_ASSERT_TRUE(result.sanitized);

    verifyCalled = false;
    verifyAccept = false;
    result = decode(bytes, crypto);
    TEST_ASSERT_TRUE(verifyCalled);
    TEST_ASSERT_EQUAL_INT(int(Status::Decoded), int(result.status));
    TEST_ASSERT_EQUAL_INT(int(Authentication::Failed), int(result.authentication));
}

void test_bare_advert_does_not_invent_optional_fields() {
    auto result = decode(advert());
    TEST_ASSERT_EQUAL_INT(int(Status::Decoded), int(result.status));
    TEST_ASSERT_TRUE(result.has_node);
    TEST_ASSERT_FALSE(result.has_position);
    TEST_ASSERT_EQUAL_STRING("", result.name);
    TEST_ASSERT_EQUAL_UINT32(0, result.role);
    TEST_ASSERT_FALSE(result.has_role);
    TEST_ASSERT_EQUAL_INT(int(Authentication::Unverified), int(result.authentication));
}

void test_every_truncated_required_advert_prefix_is_rejected() {
    const auto complete = advert({}, 3, 0x82);
    for (std::size_t size = 0; size < complete.size(); ++size) {
        Result result;
        decodeMeshCore(complete.data(), size, {}, result);
        TEST_ASSERT_EQUAL_INT(int(Status::Malformed), int(result.status));
        TEST_ASSERT_FALSE(result.has_node);
        TEST_ASSERT_FALSE(result.has_position);
    }
}

void test_null_oversize_and_result_reset() {
    Result result = decode(advert({0x81,'A'}));
    decodeMeshCore(nullptr, 100, {}, result);
    TEST_ASSERT_EQUAL_INT(int(Status::Malformed), int(result.status));
    TEST_ASSERT_FALSE(result.has_node);
    TEST_ASSERT_EQUAL_STRING("", result.name);
    const std::uint8_t tiny[1] = {};
    decodeMeshCore(tiny, 256, {}, result);
    TEST_ASSERT_EQUAL_INT(int(Status::Malformed), int(result.status));
    decodeMeshCore(tiny, std::numeric_limits<std::size_t>::max(), {}, result);
    TEST_ASSERT_EQUAL_INT(int(Status::Malformed), int(result.status));
}

void test_future_versions_and_reserved_path_mode_are_unsupported() {
    auto bytes = advert();
    bytes[0] |= 0x40;
    TEST_ASSERT_EQUAL_INT(int(Status::Unsupported), int(decode(bytes).status));
    bytes = advert();
    bytes[1] = 0xc0;
    TEST_ASSERT_EQUAL_INT(int(Status::Unsupported), int(decode(bytes).status));
    bytes[1] = 0x7f; // 63 two-byte hashes exceed the 64-byte path bound
    TEST_ASSERT_EQUAL_INT(int(Status::Malformed), int(decode(bytes).status));
}

void test_optional_fields_are_length_checked_before_publication() {
    const std::array<std::uint8_t,4> flags{{0x11,0x21,0x41,0x81}};
    for (auto flag : flags) {
        const auto result = decode(advert({flag}));
        TEST_ASSERT_EQUAL_INT(int(Status::Malformed), int(result.status));
        TEST_ASSERT_FALSE(result.has_node);
        TEST_ASSERT_FALSE(result.has_position);
    }
    TEST_ASSERT_EQUAL_INT(int(Status::Unsupported), int(decode(advert({0x01,'x'})).status));
    Bytes app(33, 'x'); app[0] = 0x81;
    const auto result = decode(advert(app));
    TEST_ASSERT_EQUAL_INT(int(Status::Unsupported), int(result.status));
    TEST_ASSERT_FALSE(result.has_node);
    TEST_ASSERT_EQUAL_INT(int(Authentication::Unverified), int(result.authentication));
}

void test_coordinate_range_and_zero_presence() {
    Bytes app{0x11}; append32(app, 0); append32(app, 0);
    auto result = decode(advert(app));
    TEST_ASSERT_TRUE(result.has_position);
    TEST_ASSERT_EQUAL_INT32(0, result.latitude_i);
    TEST_ASSERT_EQUAL_INT32(0, result.longitude_i);
    app = {0x11}; append32(app, std::uint32_t(-90000000)); append32(app, 180000000);
    result = decode(advert(app));
    TEST_ASSERT_EQUAL_INT(int(Status::Decoded), int(result.status));
    TEST_ASSERT_EQUAL_INT32(-900000000, result.latitude_i);
    TEST_ASSERT_EQUAL_INT32(1800000000, result.longitude_i);
    app = {0x11}; append32(app, 90000001); append32(app, 0);
    result = decode(advert(app));
    TEST_ASSERT_EQUAL_INT(int(Status::Malformed), int(result.status));
    TEST_ASSERT_FALSE(result.has_position);
    app = {0x11}; append32(app, 0); append32(app, 0x80000000);
    TEST_ASSERT_EQUAL_INT(int(Status::Malformed), int(decode(advert(app)).status));
}

void test_encrypted_envelopes_are_not_decoded_or_given_node_identity() {
    for (std::uint8_t type : {0,1,2,5,6,7,8}) {
        const std::size_t prefix = type == 7 ? 35 : ((type == 5 || type == 6) ? 3 : 4);
        Bytes bytes{std::uint8_t((type << 2) | 1),0};
        bytes.insert(bytes.end(), prefix + 16, 0x55);
        auto result = decode(bytes);
        TEST_ASSERT_EQUAL_INT(int(Status::Encrypted), int(result.status));
        TEST_ASSERT_FALSE(result.has_node);
        TEST_ASSERT_FALSE(result.has_position);
        TEST_ASSERT_FALSE(result.signature_present);
        bytes.pop_back();
        TEST_ASSERT_EQUAL_INT(int(Status::Malformed), int(decode(bytes).status));
    }
}

void test_ack_and_custom_payload_statuses() {
    TEST_ASSERT_EQUAL_INT(int(Status::Decoded), int(decode({0x0d,0,1,2,3,4}).status));
    TEST_ASSERT_EQUAL_INT(int(Status::Malformed), int(decode({0x0d,0,1,2,3}).status));
    for (std::uint8_t type : {9,10,11,12,13,14,15})
        TEST_ASSERT_EQUAL_INT(int(Status::Unsupported),
            int(decode({std::uint8_t((type << 2) | 1),0,0x01}).status));
}

void test_bounded_deterministic_garbage_never_reports_verified_without_adapter() {
    std::array<std::uint8_t,255> bytes{};
    std::uint32_t seed = 0x12345678;
    for (unsigned n = 0; n != 3000; ++n) {
        for (auto& byte : bytes) {
            seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
            byte = std::uint8_t(seed);
        }
        Result result;
        decodeMeshCore(bytes.data(), n % (bytes.size() + 1), {}, result);
        TEST_ASSERT_NOT_EQUAL(int(Authentication::Verified), int(result.authentication));
        TEST_ASSERT_EQUAL_CHAR('\0', result.name[sizeof(result.name)-1]);
    }
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_advert_optional_fields_and_signed_coordinates);
    RUN_TEST(test_all_routes_and_current_path_hash_sizes);
    RUN_TEST(test_signature_adapter_receives_exact_canonical_message);
    RUN_TEST(test_bare_advert_does_not_invent_optional_fields);
    RUN_TEST(test_every_truncated_required_advert_prefix_is_rejected);
    RUN_TEST(test_null_oversize_and_result_reset);
    RUN_TEST(test_future_versions_and_reserved_path_mode_are_unsupported);
    RUN_TEST(test_optional_fields_are_length_checked_before_publication);
    RUN_TEST(test_coordinate_range_and_zero_presence);
    RUN_TEST(test_encrypted_envelopes_are_not_decoded_or_given_node_identity);
    RUN_TEST(test_ack_and_custom_payload_statuses);
    RUN_TEST(test_bounded_deterministic_garbage_never_reports_verified_without_adapter);
    return UNITY_END();
}
