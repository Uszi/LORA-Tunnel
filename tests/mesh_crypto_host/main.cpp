// SPDX-License-Identifier: MIT
// Synthetic interoperability fixtures, not captured identifiers or locations.
// MeshCore advert signed independently with Python cryptography 50.0.2 using
// the publicly published RFC8032 test-1 seed; only public fixture bytes appear
// here. Meshtastic AES-CTR ciphertext was independently generated with the
// same Python package and the protocol's published default channel key.
#include <TunnelMeshDecode.h>
#include "mesh_signature.h"
#include <AES.h>
#include <Ed25519.h>
#include <RNG.h>
#include <array>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

// Some host linkers retain the library's unused key-generation functions.
// Verification needs no randomness. Fail loudly if this fixture ever calls
// those paths; this is not a random generator and cannot create test keys.
RNGClass::RNGClass() {}
RNGClass::~RNGClass() {}
void RNGClass::rand(std::uint8_t*, std::size_t) { std::abort(); }
RNGClass RNG;

using namespace tunnel::mesh;
namespace {
using Bytes = std::vector<std::uint8_t>;
Bytes hex(const char* text) {
    Bytes bytes;
    auto nibble = [](char c) -> unsigned {
        if (c >= '0' && c <= '9') return unsigned(c - '0');
        if (c >= 'a' && c <= 'f') return unsigned(c - 'a' + 10);
        assert(false); return 0;
    };
    while (*text) {
        assert(text[1]);
        bytes.push_back(std::uint8_t((nibble(text[0]) << 4) | nibble(text[1])));
        text += 2;
    }
    return bytes;
}

template<class Cipher>
bool ctrWith(const std::uint8_t* key, std::size_t keyLength, const std::uint8_t iv[16],
             const std::uint8_t* input, std::size_t length, std::uint8_t* output) {
    Cipher cipher;
    if (!cipher.setKey(key, keyLength)) return false;
    std::array<std::uint8_t,16> counter{}, stream{};
    std::memcpy(counter.data(), iv, counter.size());
    for (std::size_t offset = 0; offset < length; ++offset) {
        if ((offset % 16) == 0) {
            cipher.encryptBlock(stream.data(), counter.data());
            for (std::size_t i = counter.size(); i != 0; --i)
                if (++counter[i - 1]) break;
        }
        output[offset] = input[offset] ^ stream[offset % 16];
    }
    return true;
}

bool ctr(const std::uint8_t* key, std::size_t keyLength, const std::uint8_t iv[16],
         const std::uint8_t* input, std::size_t length, std::uint8_t* output) {
    if (!key || !iv || !input || !output || length > 239) return false;
    if (keyLength == 16) return ctrWith<AES128>(key,keyLength,iv,input,length,output);
    if (keyLength == 32) return ctrWith<AES256>(key,keyLength,iv,input,length,output);
    return false;
}

bool verify(const std::uint8_t key[32], const std::uint8_t signature[64],
            const std::uint8_t* message, std::size_t length) {
    return ed25519InputsAllowed(key,signature) && Ed25519::verify(signature,key,message,length);
}
const Crypto crypto{ctr,verify};

void checkNistCtr() {
    // NIST SP800-38A F.5.1 and F.5.5, four blocks, AES128 and AES256.
    const auto nonce = hex("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff");
    const auto plain = hex("6bc1bee22e409f96e93d7e117393172a"
                           "ae2d8a571e03ac9c9eb76fac45af8e51"
                           "30c81c46a35ce411e5fbc1191a0a52ef"
                           "f69f2445df4f9b17ad2b417be66c3710");
    const auto key128 = hex("2b7e151628aed2a6abf7158809cf4f3c");
    const auto expected128 = hex("874d6191b620e3261bef6864990db6ce"
                                 "9806f66b7970fdff8617187bb9fffdff"
                                 "5ae4df3edbd5d35e5b4f09020db03eab"
                                 "1e031dda2fbe03d1792170a0f3009cee");
    const auto key256 = hex("603deb1015ca71be2b73aef0857d7781"
                            "1f352c073b6108d72d9810a30914dff4");
    const auto expected256 = hex("601ec313775789a5b7a7f504bbf3d228"
                                 "f443e3ca4d62b59aca84e990cacaf5c5"
                                 "2b0930daa23de94ce87017ba2d84988d"
                                 "dfc9c58db67aada613c2dd08457941a6");
    Bytes actual(plain.size());
    assert(ctr(key128.data(),key128.size(),nonce.data(),plain.data(),plain.size(),actual.data()));
    assert(actual == expected128);
    assert(ctr(key256.data(),key256.size(),nonce.data(),plain.data(),plain.size(),actual.data()));
    assert(actual == expected256);
}

void checkMeshtastic() {
    const auto packet = hex("ffffffff78563412efcdab8963080000"
                            "d413e53fbc611a8c99dd40e9f317c127de159c170aeda33d");
    Result result;
    decodeMeshtastic(packet.data(),packet.size(),crypto,nullptr,0,true,result);
    assert(result.status == Status::Decoded);
    assert(result.protocol == Protocol::Meshtastic);
    assert(result.authentication == Authentication::Unverified);
    assert(result.port == 4 && result.node_id == 0x12345678);
    assert(std::strcmp(result.name,"Bench node") == 0);
    assert(std::strcmp(result.short_name,"BN") == 0);
    assert(result.hardware == 110 && result.role == 0);
    Key wrong{};
    wrong.length = 16; wrong.channel_hash = 8;
    decodeMeshtastic(packet.data(),packet.size(),crypto,&wrong,1,false,result);
    assert(result.status == Status::KeyMismatch);
    assert(!result.has_position && result.name[0] == 0);
    decodeMeshtastic(packet.data(),packet.size(),crypto,nullptr,0,false,result);
    assert(result.status == Status::Encrypted);
}

void checkMeshCore() {
    const auto packet = hex("1182010203040506"
        "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a"
        "10325465"
        "cb6983d9258d64a20418f6d89e66df0a782d203cd46a51e3ba4ef55ec9b58461"
        "dc935a38a428ff35645c58bfd2360fe573213d77a449e26c5edb7b8b1e35c20b"
        "92d012130060dad9ff54657374204d657368");
    Result result;
    decodeMeshCore(packet.data(),packet.size(),crypto,result);
    assert(result.status == Status::Decoded);
    assert(result.authentication == Authentication::Verified);
    assert(result.signature_present && result.has_node && result.has_position);
    assert(result.path_count == 2 && result.path_hash_size == 3);
    assert(result.latitude_i == 12500000 && result.longitude_i == -25000000);
    assert(result.timestamp == 0x65543210 && result.role == 2);
    assert(std::strcmp(result.name,"Test Mesh") == 0);

    for (std::size_t offset : {std::size_t(8),std::size_t(40),std::size_t(44),packet.size()-1}) {
        auto tampered = packet;
        tampered[offset] ^= 1;
        decodeMeshCore(tampered.data(),tampered.size(),crypto,result);
        assert(result.status == Status::Decoded);
        assert(result.authentication == Authentication::Failed);
    }
    auto relayed = packet;
    relayed[2] ^= 1; // mutable path is intentionally outside signed bytes
    decodeMeshCore(relayed.data(),relayed.size(),crypto,result);
    assert(result.authentication == Authentication::Verified);
    decodeMeshCore(packet.data(),packet.size(),{},result);
    assert(result.authentication == Authentication::Unverified);

    const auto order = hex("edd3f55c1a631258d69cf7a2def9de1400000000000000000000000000000010");
    auto noncanonical = packet;
    unsigned carry = 0;
    for (std::size_t i = 0; i != 32; ++i) {
        const unsigned sum = unsigned(noncanonical[76 + i]) + order[i] + carry;
        noncanonical[76 + i] = std::uint8_t(sum);
        carry = sum >> 8;
    }
    assert(!ed25519InputsAllowed(noncanonical.data()+8,noncanonical.data()+44));
    decodeMeshCore(noncanonical.data(),noncanonical.size(),crypto,result);
    assert(result.authentication == Authentication::Failed);

    const char* rejectedPoints[] = {
        "0000000000000000000000000000000000000000000000000000000000000000",
        "0100000000000000000000000000000000000000000000000000000000000000",
        "26e8958fc2b227b045c3f489f2ef98f0d5dfac05d3c63339b13802886d53fc05",
        "c7176a703d4dd84fba3c0b760d10670f2a2053fa2c39ccc64ec7fd7792ac037a",
        "ecffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f",
        "edffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f",
        "eeffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f"
    };
    for (const char* encoded : rejectedPoints) {
        const auto point = hex(encoded);
        for (std::uint8_t sign : {0,0x80}) {
            auto invalid = packet;
            std::memcpy(invalid.data()+8,point.data(),32);
            invalid[39] = (invalid[39]&0x7f)|sign;
            assert(!ed25519InputsAllowed(invalid.data()+8,invalid.data()+44));
            decodeMeshCore(invalid.data(),invalid.size(),crypto,result);
            assert(result.authentication == Authentication::Failed);
            invalid = packet;
            std::memcpy(invalid.data()+44,point.data(),32);
            invalid[75] = (invalid[75]&0x7f)|sign;
            assert(!ed25519InputsAllowed(invalid.data()+8,invalid.data()+44));
            decodeMeshCore(invalid.data(),invalid.size(),crypto,result);
            assert(result.authentication == Authentication::Failed);
        }
    }
}

void checkEd25519RejectsTrivialForgery() {
    // Identity point A and identity R with scalar S=0 satisfy the raw group
    // equation for every message. Such keys must never authenticate adverts.
    std::array<std::uint8_t,32> identity{};
    std::array<std::uint8_t,64> signature{};
    identity[0] = signature[0] = 1;
    const std::uint8_t message[] = {'n','o','t',' ','s','i','g','n','e','d'};
    assert(!verify(identity.data(),signature.data(),message,sizeof(message)));
}
} // namespace

int main() {
    checkNistCtr();
    checkMeshtastic();
    checkMeshCore();
    checkEd25519RejectsTrivialForgery();
    std::cout << "PASS: NIST AES128/256 CTR vectors; independent encrypted Meshtastic"
                 " packet; signed MeshCore advert; key/time/signature/name tampering;"
                 " mutable-path/no-verifier boundaries; weak points/noncanonical scalars.\n"
                 "Host uses real pinned rweather AES/Ed25519; production ESP32 mbedTLS"
                 " AES runtime remains a device-test requirement.\n";
}
