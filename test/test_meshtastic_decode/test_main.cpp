#include <unity.h>
#include <TunnelMeshDecode.h>
#include <array>
#include <cstring>
#include <limits>
#include <vector>

using namespace tunnel::mesh;
using Bytes=std::vector<std::uint8_t>;
namespace {
unsigned cryptoCalls=0;
std::array<std::uint8_t,16> observedNonce{};
std::array<std::uint8_t,32> observedKey{};
std::size_t observedKeyLength=0;
bool identityCtr(const std::uint8_t* key,std::size_t length,const std::uint8_t nonce[16],
                 const std::uint8_t* input,std::size_t size,std::uint8_t* output) {
  ++cryptoCalls;observedKeyLength=length;
  std::memcpy(observedNonce.data(),nonce,16);std::memcpy(observedKey.data(),key,length);
  std::memcpy(output,input,size);return true;
}
bool rejectCtr(const std::uint8_t*,std::size_t,const std::uint8_t[16],
               const std::uint8_t*,std::size_t size,std::uint8_t* output) {
  ++cryptoCalls;std::memset(output,0xff,size);return true;
}
bool selectiveCtr(const std::uint8_t* key,std::size_t,const std::uint8_t[16],
                  const std::uint8_t* input,std::size_t size,std::uint8_t* output) {
  ++cryptoCalls;if(key[0]!=0x42){std::memset(output,0xff,size);return true;}
  std::memcpy(output,input,size);return true;
}
void varint(Bytes& bytes,std::uint64_t n) {
  while(n>127){bytes.push_back(std::uint8_t(n)|0x80);n>>=7;}bytes.push_back(std::uint8_t(n));
}
void integer(Bytes& bytes,unsigned field,std::uint64_t n) {varint(bytes,field<<3);varint(bytes,n);}
void fixed(Bytes& bytes,unsigned field,std::uint32_t n) {
  varint(bytes,(field<<3)|5);for(unsigned i=0;i<4;++i)bytes.push_back(std::uint8_t(n>>(8*i)));
}
void blob(Bytes& bytes,unsigned field,const Bytes& value) {
  varint(bytes,(field<<3)|2);varint(bytes,value.size());bytes.insert(bytes.end(),value.begin(),value.end());
}
void text(Bytes& bytes,unsigned field,const char* value) {
  blob(bytes,field,Bytes(value,value+std::strlen(value)));
}
Bytes data(unsigned port,const Bytes& payload) {Bytes bytes;integer(bytes,1,port);blob(bytes,2,payload);return bytes;}
Bytes packet(const Bytes& payload,std::uint8_t hash=8) {
  Bytes bytes={255,255,255,255,0x78,0x56,0x34,0x12,0xef,0xcd,0xab,0x89,0x63,hash,0,0};
  bytes.insert(bytes.end(),payload.begin(),payload.end());return bytes;
}
Result plain(const Bytes& payload) {
  auto bytes=packet(payload);Key key;key.channel_hash=8;Result out;
  decodeMeshtastic(bytes.data(),bytes.size(),Crypto{},&key,1,false,out);return out;
}
Result publicFrame(const Bytes& payload,bool wrong=false,std::uint8_t hash=8) {
  auto bytes=packet(payload,hash);Crypto crypto;crypto.ctr=wrong?rejectCtr:identityCtr;Result out;
  decodeMeshtastic(bytes.data(),bytes.size(),crypto,nullptr,0,true,out);return out;
}
}
void setUp() {cryptoCalls=0;observedNonce.fill(0);observedKey.fill(0);observedKeyLength=0;}
void tearDown() {}
void test_nodeinfo_plaintext_and_zero_fields() {
  Bytes node;text(node,1,"!12345678");text(node,2,"Bench node");text(node,3,"BN");integer(node,5,110);integer(node,7,0);
  const auto out=plain(data(4,node));
  TEST_ASSERT_EQUAL(int(Status::Decoded),int(out.status));TEST_ASSERT_TRUE(out.has_node);
  TEST_ASSERT_EQUAL_STRING("Bench node",out.name);TEST_ASSERT_EQUAL_STRING("BN",out.short_name);
  TEST_ASSERT_EQUAL_UINT32(110,out.hardware);TEST_ASSERT_EQUAL_HEX32(0x12345678,out.node_id);
  TEST_ASSERT_TRUE(out.has_hardware);TEST_ASSERT_TRUE(out.has_role);
  TEST_ASSERT_EQUAL(int(Authentication::Unverified),int(out.authentication));
}
void test_default_key_and_nonce_contract() {
  Bytes node;text(node,2,"Synthetic");const auto out=publicFrame(data(4,node));
  const std::uint8_t expectedKey[]={0xd4,0xf1,0xbb,0x3a,0x20,0x29,7,0x59,0xf0,0xbc,0xff,0xab,0xcf,0x4e,0x69,1};
  const std::uint8_t expectedNonce[]={0xef,0xcd,0xab,0x89,0,0,0,0,0x78,0x56,0x34,0x12,0,0,0,0};
  TEST_ASSERT_EQUAL_UINT(1,cryptoCalls);TEST_ASSERT_EQUAL_UINT(16,observedKeyLength);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedKey,observedKey.data(),16);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedNonce,observedNonce.data(),16);
  TEST_ASSERT_EQUAL(int(Status::Decoded),int(out.status));
  // This fixture verifies the adapter contract, not AES itself. The adapter's
  // independent cryptographic known-answer tests are separate.
}
void test_public_key_renamed_channel_and_aead_hint() {
  Bytes node;text(node,2,"Renamed");
  TEST_ASSERT_EQUAL(int(Status::Decoded),int(publicFrame(data(4,node),false,0x55).status));
  cryptoCalls=0;const auto unsupported=publicFrame(data(4,node),false,0xa6);
  TEST_ASSERT_EQUAL_UINT(0,cryptoCalls);TEST_ASSERT_EQUAL(int(Status::Unsupported),int(unsupported.status));
}
void test_negative_coordinates_altitude_and_explicit_zero() {
  Bytes position;fixed(position,1,std::uint32_t(-338651430));fixed(position,2,1512099000);
  integer(position,3,static_cast<std::uint64_t>(std::int64_t(-27)));fixed(position,4,1700000000);integer(position,23,24);
  const auto out=plain(data(3,position));TEST_ASSERT_EQUAL(int(Status::Decoded),int(out.status));
  TEST_ASSERT_TRUE(out.has_position);TEST_ASSERT_EQUAL_INT32(-338651430,out.latitude_i);
  TEST_ASSERT_EQUAL_INT32(1512099000,out.longitude_i);TEST_ASSERT_TRUE(out.has_altitude);TEST_ASSERT_EQUAL_INT32(-27,out.altitude);
  TEST_ASSERT_TRUE(out.has_time);TEST_ASSERT_EQUAL_UINT32(1700000000,out.timestamp);TEST_ASSERT_EQUAL_UINT8(24,out.precision_bits);
  Bytes zero;fixed(zero,1,0);fixed(zero,2,0);integer(zero,3,0);
  const auto z=plain(data(3,zero));TEST_ASSERT_TRUE(z.has_position);TEST_ASSERT_TRUE(z.has_altitude);TEST_ASSERT_FALSE(z.has_time);
}
void test_missing_coordinates_do_not_create_position() {
  Bytes p;fixed(p,1,123);fixed(p,4,0);const auto out=plain(data(3,p));
  TEST_ASSERT_EQUAL(int(Status::Decoded),int(out.status));TEST_ASSERT_FALSE(out.has_position);TEST_ASSERT_FALSE(out.has_time);
  const auto empty=plain(data(4,{}));TEST_ASSERT_FALSE(empty.has_node);TEST_ASSERT_EQUAL_STRING("",empty.name);
  TEST_ASSERT_FALSE(empty.has_hardware);TEST_ASSERT_FALSE(empty.has_role);
}
void test_invalid_coordinate_range_and_known_wire_type() {
  Bytes p;fixed(p,1,900000001);fixed(p,2,0);
  TEST_ASSERT_EQUAL(int(Status::Malformed),int(plain(data(3,p)).status));
  p.clear();integer(p,1,1);fixed(p,2,2);TEST_ASSERT_EQUAL(int(Status::Malformed),int(plain(data(3,p)).status));
  p.clear();fixed(p,1,0);fixed(p,2,1800000001);TEST_ASSERT_EQUAL(int(Status::Malformed),int(plain(data(3,p)).status));
}
void test_device_metrics_presence_and_finite_voltage() {
  Bytes device;integer(device,1,101);fixed(device,2,0x40700000); // 3.75 V
  Bytes telemetry;fixed(telemetry,1,1700000001);blob(telemetry,2,device);
  const auto out=plain(data(67,telemetry));TEST_ASSERT_EQUAL(int(Status::Decoded),int(out.status));
  TEST_ASSERT_TRUE(out.has_battery);TEST_ASSERT_EQUAL_UINT32(101,out.battery);TEST_ASSERT_TRUE(out.has_voltage);TEST_ASSERT_EQUAL_FLOAT(3.75f,out.voltage);
  device.clear();fixed(device,2,0x7fc00000);telemetry.clear();blob(telemetry,2,device);
  TEST_ASSERT_EQUAL(int(Status::Malformed),int(plain(data(67,telemetry)).status));
  telemetry.clear();blob(telemetry,3,{});TEST_ASSERT_EQUAL(int(Status::Unsupported),int(plain(data(67,telemetry)).status));
}
void test_text_is_bounded_sanitized_and_signature_unverified() {
  Bytes message(190,'X');message[1]=0;message[2]=0xff;
  Bytes d=data(1,message);const auto out=plain(d);
  TEST_ASSERT_TRUE(out.has_text);TEST_ASSERT_TRUE(out.sanitized);TEST_ASSERT_EQUAL_UINT(159,std::strlen(out.text));
  TEST_ASSERT_EQUAL_CHAR('?',out.text[1]);TEST_ASSERT_EQUAL_CHAR('?',out.text[2]);
  d=data(1,Bytes{'h','i'});blob(d,10,Bytes(64,0x42));const auto signedOut=plain(d);
  TEST_ASSERT_TRUE(signedOut.signature_present);TEST_ASSERT_EQUAL(int(Authentication::Unverified),int(signedOut.authentication));
  d=data(1,Bytes{'h','i'});blob(d,10,Bytes(63,0x42));TEST_ASSERT_EQUAL(int(Status::Malformed),int(plain(d).status));
}
void test_duplicate_known_fields_and_oversized_varints_rejected() {
  Bytes node;text(node,2,"One");text(node,2,"Two");
  TEST_ASSERT_EQUAL(int(Status::Malformed),int(plain(data(4,node)).status));
  Bytes d={8,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x02};
  TEST_ASSERT_EQUAL(int(Status::Malformed),int(plain(d).status));
  d={8,4,18,0xff,0xff,0xff,0xff,0x0f};TEST_ASSERT_EQUAL(int(Status::Malformed),int(plain(d).status));
}
void test_unknown_fields_skip_safely_and_unknown_port_is_unsupported() {
  Bytes node;text(node,2,"Node");fixed(node,100,0xffffffff);blob(node,101,Bytes{1,2,3});
  const auto out=plain(data(4,node));TEST_ASSERT_EQUAL(int(Status::Decoded),int(out.status));TEST_ASSERT_EQUAL_STRING("Node",out.name);
  TEST_ASSERT_EQUAL(int(Status::Unsupported),int(plain(data(256,Bytes{0xff,0x81})).status));
  TEST_ASSERT_EQUAL(int(Status::Malformed),int(plain(Bytes{0}).status));
}
void test_wrong_key_never_leaks_partial_result() {
  Bytes node;text(node,2,"Should not appear");const auto out=publicFrame(data(4,node),true);
  TEST_ASSERT_EQUAL(int(Status::KeyMismatch),int(out.status));TEST_ASSERT_FALSE(out.has_node);
  TEST_ASSERT_EQUAL_STRING("",out.name);TEST_ASSERT_FALSE(out.has_position);TEST_ASSERT_FALSE(out.has_text);
}
void test_key_hash_collisions_try_later_keys() {
  Key keys[3];keys[0].length=16;keys[0].channel_hash=8;
  keys[1].length=32;keys[1].channel_hash=8;keys[1].bytes[0]=0x42;
  keys[2].length=16;keys[2].channel_hash=7;keys[2].bytes[0]=0x42;
  Bytes node;text(node,2,"Second key");const auto bytes=packet(data(4,node));Crypto crypto;crypto.ctr=selectiveCtr;Result out;
  decodeMeshtastic(bytes.data(),bytes.size(),crypto,keys,3,false,out);
  TEST_ASSERT_EQUAL_UINT(2,cryptoCalls);TEST_ASSERT_EQUAL_STRING("Second key",out.name);TEST_ASSERT_EQUAL(int(Status::Decoded),int(out.status));
}
void test_unmatched_keys_and_missing_crypto_keep_raw_packet_status() {
  Bytes node;text(node,2,"Unknown");const auto bytes=packet(data(4,node));Result out;Key key;key.channel_hash=7;
  decodeMeshtastic(bytes.data(),bytes.size(),Crypto{},&key,1,false,out);
  TEST_ASSERT_EQUAL(int(Status::Encrypted),int(out.status));TEST_ASSERT_FALSE(out.has_node);
  decodeMeshtastic(bytes.data(),bytes.size(),Crypto{},nullptr,0,true,out);
  TEST_ASSERT_EQUAL(int(Status::KeyMismatch),int(out.status));
}
void test_null_oversize_and_all_truncated_frame_bounds() {
  const std::uint8_t tiny[]={0};Result out;
  decodeMeshtastic(nullptr,20,Crypto{},nullptr,0,true,out);TEST_ASSERT_EQUAL(int(Status::Malformed),int(out.status));
  decodeMeshtastic(tiny,256,Crypto{},nullptr,0,true,out);TEST_ASSERT_EQUAL(int(Status::Malformed),int(out.status));
  decodeMeshtastic(tiny,std::numeric_limits<std::size_t>::max(),Crypto{},nullptr,0,true,out);TEST_ASSERT_EQUAL(int(Status::Malformed),int(out.status));
  Bytes node;text(node,2,"Name");const auto bytes=packet(data(4,node));Key key;key.channel_hash=8;
  for(std::size_t n=0;n<16;++n){decodeMeshtastic(bytes.data(),n,Crypto{},&key,1,false,out);TEST_ASSERT_EQUAL(int(Status::Malformed),int(out.status));}
  // Truncating inside the node name must reject even though the outer header
  // remains readable; no partially parsed name is published.
  decodeMeshtastic(bytes.data(),bytes.size()-1,Crypto{},&key,1,false,out);
  TEST_ASSERT_EQUAL(int(Status::Malformed),int(out.status));TEST_ASSERT_EQUAL_STRING("",out.name);
}
void test_deterministic_malformed_inputs_are_bounded() {
  std::uint32_t state=0x937baf11;std::array<std::uint8_t,255> bytes{};Key key;
  for(unsigned n=0;n<2500;++n){
    const std::size_t size=17+n%239;
    for(std::size_t i=0;i<size;++i){state=state*1664525+1013904223;bytes[i]=std::uint8_t(state>>24);}
    key.channel_hash=bytes[13];Result out;
    decodeMeshtastic(bytes.data(),size,Crypto{},&key,1,false,out);
    TEST_ASSERT_NOT_EQUAL(int(Authentication::Verified),int(out.authentication));
    TEST_ASSERT_EQUAL_CHAR(0,out.name[sizeof(out.name)-1]);TEST_ASSERT_EQUAL_CHAR(0,out.text[sizeof(out.text)-1]);
  }
}
int main(int,char**) {
  UNITY_BEGIN();
  RUN_TEST(test_nodeinfo_plaintext_and_zero_fields);RUN_TEST(test_default_key_and_nonce_contract);
  RUN_TEST(test_public_key_renamed_channel_and_aead_hint);RUN_TEST(test_negative_coordinates_altitude_and_explicit_zero);
  RUN_TEST(test_missing_coordinates_do_not_create_position);RUN_TEST(test_invalid_coordinate_range_and_known_wire_type);
  RUN_TEST(test_device_metrics_presence_and_finite_voltage);RUN_TEST(test_text_is_bounded_sanitized_and_signature_unverified);
  RUN_TEST(test_duplicate_known_fields_and_oversized_varints_rejected);RUN_TEST(test_unknown_fields_skip_safely_and_unknown_port_is_unsupported);
  RUN_TEST(test_wrong_key_never_leaks_partial_result);RUN_TEST(test_key_hash_collisions_try_later_keys);
  RUN_TEST(test_unmatched_keys_and_missing_crypto_keep_raw_packet_status);RUN_TEST(test_null_oversize_and_all_truncated_frame_bounds);
  RUN_TEST(test_deterministic_malformed_inputs_are_bounded);
  return UNITY_END();
}
