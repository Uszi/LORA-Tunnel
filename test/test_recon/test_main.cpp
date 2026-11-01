#include <unity.h>
#include <array>
#include <cstring>
#include <vector>
#include <limits>
#include "recon.h"
#include <opendroneid.h>

using namespace tunnel::recon;
void setUp() {}
void tearDown() {}
static const uint8_t publicMac[6]={0x10,0x20,0x30,0x40,0x50,0x60};
static std::vector<uint8_t> ad(uint8_t type,const std::vector<uint8_t>& bytes) {
  std::vector<uint8_t> result{uint8_t(bytes.size()+1),type};
  result.insert(result.end(),bytes.begin(),bytes.end());return result;
}
static Result ble(const std::vector<uint8_t>& bytes) {
  return classifyBle(publicMac,0,bytes.data(),bytes.size());
}
static std::vector<uint8_t> iBeacon(size_t extra=0) {
  std::vector<uint8_t> data(25+extra,0);
  data[0]=0x4c;data[2]=2;data[3]=0x15;
  for(unsigned i=0;i<16;++i)data[4+i]=uint8_t(i);
  data[20]=0x12;data[21]=0x34;data[22]=0xab;data[23]=0xcd;data[24]=0xc5;
  return ad(0xff,data);
}
static std::vector<uint8_t> remoteAd(const uint8_t* encoded,size_t n=25) {
  std::vector<uint8_t> body{0xfa,0xff,0x0d,0x03};
  body.insert(body.end(),encoded,encoded+n);return ad(0x16,body);
}
void test_oui_is_a_candidate_not_camera_proof() {
  const auto alpr=classifyWifi("00:04:7D:11:22:33","");
  TEST_ASSERT_TRUE(alpr.matched());TEST_ASSERT_EQUAL(int(Category::Alpr),int(alpr.category));
  TEST_ASSERT_EQUAL(int(Confidence::Low),int(alpr.confidence));
  TEST_ASSERT_NOT_NULL(strstr(alpr.label,"candidate"));
  const auto ring=classifyWifi("AC:9F:C3:11:22:33","");
  TEST_ASSERT_EQUAL(int(Category::Ring),int(ring.category));
  TEST_ASSERT_TRUE(ring.confidence<Confidence::High);
  const auto camera=classifyWifi("00:40:8C:11:22:33","");
  TEST_ASSERT_EQUAL(int(Category::Camera),int(camera.category));
  TEST_ASSERT_FALSE(classifyWifi("24:6F:28:11:22:33","").matched());
}
void test_random_addresses_cannot_supply_vendor_evidence() {
  const uint8_t axon[]={0,0x25,0xdf,1,2,3};const uint8_t flags[]={2,1,6};
  for(uint8_t type:{1,3,255}){
    const auto result=classifyBle(axon,type,flags,sizeof(flags));
    TEST_ASSERT_FALSE(result.matched());
  }
  TEST_ASSERT_EQUAL(int(Category::Axon),int(classifyBle(axon,0,flags,sizeof(flags)).category));
  TEST_ASSERT_FALSE(classifyWifi("82:6B:F2:11:22:33","").matched());
  TEST_ASSERT_FALSE(classifyWifi("02:C0:CA:11:22:33","").matched());
}
void test_axon_composite_and_marker_and_name_grades() {
  auto data=ad(0xff,{0x4d,3});const auto service=ad(3,{0x81,0xfc});
  data.insert(data.end(),service.begin(),service.end());const auto strong=ble(data);
  TEST_ASSERT_EQUAL(int(Category::Axon),int(strong.category));
  TEST_ASSERT_EQUAL(int(Confidence::High),int(strong.confidence));
  const auto marker=ble(ad(0xff,{'E','C','I','V','E','D','C','W','B'}));
  TEST_ASSERT_EQUAL(int(Category::Axon),int(marker.category));
  TEST_ASSERT_EQUAL(int(Confidence::Medium),int(marker.confidence));
  const auto name=ble(ad(9,{'A','x','o','n',' ','t','e','s','t'}));
  TEST_ASSERT_EQUAL(int(Confidence::Low),int(name.confidence));
}
void test_custom_uuid_cannot_impersonate_an_assigned_service() {
  std::vector<uint8_t> data(16,0);data[12]=0x81;data[13]=0xfc;
  const auto result=ble(ad(7,data));
  // Dragon's compatibility path is a weak hint, never assigned-service proof.
  TEST_ASSERT_TRUE(result.confidence<=Confidence::Low);
  const uint8_t base[]={0xfb,0x34,0x9b,0x5f,0x80,0,0,0x80,0,0x10,0,0};
  memcpy(data.data(),base,12);
  TEST_ASSERT_EQUAL(int(Confidence::Medium),int(ble(ad(7,data)).confidence));
}
void test_ibeacon_decodes_network_order_and_signed_power() {
  const auto result=ble(iBeacon());
  TEST_ASSERT_EQUAL(int(Category::IBeacon),int(result.category));
  TEST_ASSERT_EQUAL(int(Confidence::High),int(result.confidence));
  TEST_ASSERT_TRUE(result.beacon.valid);TEST_ASSERT_EQUAL_HEX16(0x1234,result.beacon.major);
  TEST_ASSERT_EQUAL_HEX16(0xabcd,result.beacon.minor);TEST_ASSERT_EQUAL_INT8(-59,result.beacon.measured_power);
  TEST_ASSERT_EQUAL_UINT8(15,result.beacon.uuid[15]);
  const auto extended=ble(iBeacon(1));
  TEST_ASSERT_EQUAL(int(Confidence::Low),int(extended.confidence));TEST_ASSERT_FALSE(extended.beacon.valid);
}
void test_findmy_and_pairing_remain_accessory_candidates() {
  TEST_ASSERT_FALSE(ble(ad(0xff,{0x4c,0})).matched());
  std::vector<uint8_t> data(29,0);data[0]=0x4c;data[2]=0x12;data[3]=0x19;
  const auto findmy=ble(ad(0xff,data));
  TEST_ASSERT_EQUAL(int(Category::AirTag),int(findmy.category));
  TEST_ASSERT_EQUAL(int(Confidence::Medium),int(findmy.confidence));
  TEST_ASSERT_NOT_NULL(strstr(findmy.evidence,"another accessory"));
  data[2]=7;const auto pair=ble(ad(0xff,data));
  TEST_ASSERT_EQUAL(int(Confidence::Low),int(pair.confidence));
  TEST_ASSERT_NOT_NULL(strstr(pair.evidence,"AirPods"));
}
void test_fmdn_requires_own_frame_and_length() {
  const auto eddy=ble(ad(0x16,{0xaa,0xfe,0x00,1,2,3}));
  TEST_ASSERT_TRUE(eddy.confidence<=Confidence::Low);
  std::vector<uint8_t> data(23,0);data[0]=0xaa;data[1]=0xfe;data[2]=0x40;
  const auto fmdn=ble(ad(0x16,data));
  TEST_ASSERT_EQUAL(int(Category::Tracker),int(fmdn.category));
  TEST_ASSERT_EQUAL(int(Confidence::High),int(fmdn.confidence));
}
void test_malformed_ad_and_segment_boundaries_do_not_match() {
  auto bytes=iBeacon();bytes.push_back(0x20);bytes.push_back(0xff);
  auto result=ble(bytes);TEST_ASSERT_FALSE(result.payload_valid);TEST_ASSERT_FALSE(result.matched());
  const uint8_t crossing[]={4,0xff,0x4d,3,2,1,6};
  result=classifyBle(publicMac,0,crossing,sizeof(crossing),3);
  TEST_ASSERT_FALSE(result.payload_valid);TEST_ASSERT_FALSE(result.matched());
  auto valid=iBeacon();valid.insert(valid.begin(),0);
  result=classifyBle(publicMac,0,valid.data(),valid.size(),1);
  TEST_ASSERT_TRUE(result.beacon.valid);
  const uint8_t shortByte=0;
  TEST_ASSERT_FALSE(classifyBle(publicMac,0,&shortByte,kMaxBleBytes+1).payload_valid);
  TEST_ASSERT_FALSE(classifyBle(publicMac,0,nullptr,1).payload_valid);
}
void test_remote_id_requires_whole_message_and_keeps_id_type() {
  ODID_BasicID_data data{};data.IDType=ODID_IDTYPE_UTM_ASSIGNED_UUID;data.UAType=ODID_UATYPE_HELICOPTER_OR_MULTIROTOR;
  for(unsigned i=0;i<20;++i)data.UASID[i]=char(i+1);
  ODID_BasicID_encoded encoded{};TEST_ASSERT_EQUAL(ODID_SUCCESS,encodeBasicIDMessage(&encoded,&data));
  const auto result=ble(remoteAd(reinterpret_cast<uint8_t*>(&encoded)));
  TEST_ASSERT_TRUE(result.remote_id.decoded);TEST_ASSERT_TRUE(result.remote_id.has_id);
  TEST_ASSERT_EQUAL_UINT8(3,result.remote_id.id_type);TEST_ASSERT_EQUAL_UINT8(2,result.remote_id.aircraft_type);
  TEST_ASSERT_EQUAL_STRING("0102030405060708090A0B0C0D0E0F1011121314",result.remote_id.id);
  TEST_ASSERT_FALSE(result.remote_id.has_location);
  const auto shortResult=ble(remoteAd(reinterpret_cast<uint8_t*>(&encoded),24));
  TEST_ASSERT_FALSE(shortResult.remote_id.decoded);TEST_ASSERT_TRUE(shortResult.confidence<Confidence::High);
}
void test_remote_id_location_sentinels_and_operator_type() {
  ODID_Location_data loc{};odid_initLocationData(&loc);loc.Latitude=42;loc.Longitude=-73;
  loc.AltitudeGeo=INV_ALT;loc.SpeedHorizontal=INV_SPEED_H;
  ODID_Location_encoded encoded{};TEST_ASSERT_EQUAL(ODID_SUCCESS,encodeLocationMessage(&encoded,&loc));
  auto result=ble(remoteAd(reinterpret_cast<uint8_t*>(&encoded)));
  TEST_ASSERT_TRUE(result.remote_id.has_location);TEST_ASSERT_FALSE(result.remote_id.has_altitude);
  TEST_ASSERT_FALSE(result.remote_id.has_speed);
  ODID_System_data system{};odid_initSystemData(&system);
  system.OperatorLatitude=41;system.OperatorLongitude=-72;system.OperatorLocationType=ODID_OPERATOR_LOCATION_TYPE_TAKEOFF;
  ODID_System_encoded sys{};TEST_ASSERT_EQUAL(ODID_SUCCESS,encodeSystemMessage(&sys,&system));
  result=ble(remoteAd(reinterpret_cast<uint8_t*>(&sys)));
  TEST_ASSERT_TRUE(result.remote_id.has_operator);TEST_ASSERT_EQUAL_UINT8(0,result.remote_id.operator_location_type);
  // These are advertised takeoff coordinates, never automatically a live pilot.
  TEST_ASSERT_FALSE(result.remote_id.has_location);
}
void test_wifi_remote_id_pack_and_directed_probe_identity() {
  ODID_BasicID_data basic{};basic.IDType=ODID_IDTYPE_SERIAL_NUMBER;basic.UAType=ODID_UATYPE_HELICOPTER_OR_MULTIROTOR;
  strcpy(basic.UASID,"SYNTHETIC-DRONE");ODID_BasicID_encoded encoded{};
  TEST_ASSERT_EQUAL(ODID_SUCCESS,encodeBasicIDMessage(&encoded,&basic));
  std::vector<uint8_t> frame(36,0);frame[0]=0x80;memcpy(frame.data()+10,publicMac,6);memcpy(frame.data()+16,publicMac,6);
  std::vector<uint8_t> ie{0xfa,0x0b,0xbc,0x0d,1,0xf2,25,1};
  const auto raw=reinterpret_cast<const uint8_t*>(&encoded);ie.insert(ie.end(),raw,raw+25);
  frame.push_back(221);frame.push_back(uint8_t(ie.size()));frame.insert(frame.end(),ie.begin(),ie.end());
  auto result=classifyWifi(publicMac,"",frame.data(),frame.size());
  TEST_ASSERT_TRUE(result.remote_id.decoded);TEST_ASSERT_EQUAL_STRING("SYNTHETIC-DRONE",result.remote_id.id);
  frame[45]=2; // pack declares two messages but carries one
  result=classifyWifi(publicMac,"",frame.data(),frame.size());TEST_ASSERT_FALSE(result.remote_id.decoded);
  std::vector<uint8_t> probe(24,0);probe[0]=0x40;memcpy(probe.data()+10,publicMac,6);
  probe.insert(probe.end(),{0,8,'A','B','3','-','T','E','S','T'});
  result=classifyWifi(publicMac,"",probe.data(),probe.size());TEST_ASSERT_FALSE(result.matched());
}
void test_wifi_malformed_tail_and_null_boundaries() {
  const uint8_t tiny=0;
  TEST_ASSERT_FALSE(classifyWifi(publicMac,"",&tiny,kMaxWifiBytes+1).payload_valid);
  TEST_ASSERT_FALSE(classifyWifi(publicMac,"",nullptr,24).payload_valid);
  std::vector<uint8_t> frame(36,0);frame[0]=0x80;frame.insert(frame.end(),{0,4,'A'});
  TEST_ASSERT_FALSE(classifyWifi(publicMac,"",frame.data(),frame.size()).payload_valid);
  uint8_t mac[6]={9,9,9,9,9,9};TEST_ASSERT_FALSE(parseAddress("00:25:DF",mac));
  TEST_ASSERT_EQUAL_UINT8(9,mac[0]);TEST_ASSERT_FALSE(parseAddress("00:25:DF:01:02:03more",mac));
  TEST_ASSERT_TRUE(parseAddress("00:25:df:01:02:03",mac));TEST_ASSERT_EQUAL_HEX8(0xdf,mac[2]);
}
void test_flock_service_and_name_remain_distinct_evidence() {
  const auto name=ble(ad(9,{'F','S','-','B','E','C','4','6','A'}));
  TEST_ASSERT_EQUAL(int(Category::Flock),int(name.category));
  TEST_ASSERT_EQUAL(int(Confidence::Low),int(name.confidence));
  const auto service=ble(ad(7,{0x6f,0x2e,0x17,0xdf,0x14,0x18,0xe5,0x9f,0xa8,0x46,0x32,0x95,0x38,0xbb,0xcc,0xe8}));
  TEST_ASSERT_EQUAL(int(Category::Flock),int(service.category));
  TEST_ASSERT_EQUAL(int(Confidence::High),int(service.confidence));
  TEST_ASSERT_NOT_NULL(strstr(service.evidence,"camera presence unverified"));
}
void test_remote_id_nan_validates_nested_lengths() {
  ODID_BasicID_data basic{};basic.IDType=ODID_IDTYPE_SERIAL_NUMBER;basic.UAType=ODID_UATYPE_HELICOPTER_OR_MULTIROTOR;
  strcpy(basic.UASID,"SYNTHETIC-NAN");ODID_BasicID_encoded encoded{};
  TEST_ASSERT_EQUAL(ODID_SUCCESS,encodeBasicIDMessage(&encoded,&basic));
  std::vector<uint8_t> frame(24,0);frame[0]=0xd0;memcpy(frame.data()+10,publicMac,6);
  frame.insert(frame.end(),{4,9,0x50,0x6f,0x9a,0x13,3,39,0,0x88,0x69,0x19,0x9d,0x92,9,0,0,0x10,29,1,0xf2,25,1});
  const auto raw=reinterpret_cast<const uint8_t*>(&encoded);frame.insert(frame.end(),raw,raw+25);
  auto result=classifyWifi(publicMac,"",frame.data(),frame.size());
  TEST_ASSERT_TRUE(result.remote_id.decoded);TEST_ASSERT_EQUAL_STRING("SYNTHETIC-NAN",result.remote_id.id);
  result=classifyWifi(publicMac,"",frame.data(),frame.size()-1);
  TEST_ASSERT_FALSE(result.remote_id.decoded);
  frame[42]=28; // service-information length no longer matches attribute length
  result=classifyWifi(publicMac,"",frame.data(),frame.size());TEST_ASSERT_FALSE(result.remote_id.decoded);
}
int main(int,char**) {
  UNITY_BEGIN();
  RUN_TEST(test_oui_is_a_candidate_not_camera_proof);
  RUN_TEST(test_random_addresses_cannot_supply_vendor_evidence);
  RUN_TEST(test_axon_composite_and_marker_and_name_grades);
  RUN_TEST(test_custom_uuid_cannot_impersonate_an_assigned_service);
  RUN_TEST(test_ibeacon_decodes_network_order_and_signed_power);
  RUN_TEST(test_findmy_and_pairing_remain_accessory_candidates);
  RUN_TEST(test_fmdn_requires_own_frame_and_length);
  RUN_TEST(test_malformed_ad_and_segment_boundaries_do_not_match);
  RUN_TEST(test_remote_id_requires_whole_message_and_keeps_id_type);
  RUN_TEST(test_remote_id_location_sentinels_and_operator_type);
  RUN_TEST(test_wifi_remote_id_pack_and_directed_probe_identity);
  RUN_TEST(test_wifi_malformed_tail_and_null_boundaries);
  RUN_TEST(test_flock_service_and_name_remain_distinct_evidence);
  RUN_TEST(test_remote_id_nan_validates_nested_lengths);
  return UNITY_END();
}
