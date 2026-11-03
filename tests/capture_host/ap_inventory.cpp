#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "app_types.h"
#include "text_sanitize.h"
using namespace tunnel;
namespace tunnel { Counters counters; }
namespace {
enum wifi_auth_mode_t { WIFI_AUTH_OPEN,WIFI_AUTH_WEP,WIFI_AUTH_WPA_PSK,WIFI_AUTH_WPA2_PSK,WIFI_AUTH_WPA_WPA2_PSK,WIFI_AUTH_WPA2_ENTERPRISE,WIFI_AUTH_WPA3_PSK,WIFI_AUTH_WPA2_WPA3_PSK,WIFI_AUTH_WAPI_PSK,WIFI_AUTH_OWE };
struct wifi_ap_record_t {uint8_t bssid[6]{},ssid[33]{};uint8_t primary=0;int8_t rssi=0;wifi_auth_mode_t authmode=WIFI_AUTH_OPEN;uint8_t unused[44]{};};
static_assert(sizeof(wifi_ap_record_t)==92,"mirror the pinned SDK inventory size");
std::array<wifi_ap_record_t,100> source;
std::array<CaptureObservation,64> delivered;
unsigned checks=0,count=0,popped=0,clears=0,submitted=0,bleStarts=0,failPop=0;
bool failCount=false;
void expect(bool value,const char* description){++checks;if(!value){std::fprintf(stderr,"FAIL %s\n",description);std::exit(1);}}
constexpr int ESP_OK=0,ESP_FAIL=-1;
int esp_wifi_scan_get_ap_num(uint16_t* out){if(failCount)return ESP_FAIL;*out=static_cast<uint16_t>(count);return ESP_OK;}
int esp_wifi_scan_get_ap_record(wifi_ap_record_t* out){if(popped==count || (failPop&&popped+1==failPop))return ESP_FAIL;*out=source[popped++];return ESP_OK;}
void esp_wifi_clear_ap_list(){++clears;}
void startBle(){++bleStarts;}
void hostSubmit(CaptureObservation& event){if(submitted>=64)std::abort();delivered[submitted++]=event;}
size_t strlcpy(char* out,const char* in,size_t capacity){const auto n=std::strlen(in);if(capacity){const auto copy=std::min(n,capacity-1);std::memcpy(out,in,copy);out[copy]=0;}return n;}
#include "ap_actual.inc"
void reset(unsigned n){count=n;popped=clears=submitted=bleStarts=failPop=0;failCount=false;counters.ingress_drops=0;counters.radio_errors=0;counters.scan_cycles=0;wifiResultCount=wifiResultNext=0;
 for(unsigned i=0;i<n;++i){auto& ap=source[i];ap=wifi_ap_record_t{};for(unsigned j=0;j<6;++j)ap.bssid[j]=static_cast<uint8_t>(i+j);for(unsigned j=0;j<32;++j)ap.ssid[j]=static_cast<uint8_t>('A'+(i+j)%26);ap.ssid[32]=0;ap.primary=static_cast<uint8_t>(1+i%11);ap.rssi=static_cast<int8_t>(-20-i);ap.authmode=static_cast<wifi_auth_mode_t>(i%10);std::memset(ap.unused,0xa5,sizeof(ap.unused));}}
}
namespace tunnel { void submit(CaptureObservation& event){hostSubmit(event);} }
int main(){
 expect(sizeof(aps)==3072 && 64*sizeof(wifi_ap_record_t)-sizeof(aps)==2816,"64 unchanged slots save exactly2816 bytes");
 reset(66);source[1].ssid[5]=0;source[2].ssid[32]='X';collectWifi();
 expect(wifiResultCount==64 && popped==64 && clears==1 && counters.ingress_drops==2,"cap64 preserves count/drop semantics and clears unread SDK nodes");
 for(unsigned i=0;i<64;++i){expect(std::memcmp(aps[i].ssid,source[i].ssid,33)==0 && std::memcmp(aps[i].bssid,source[i].bssid,6)==0,"all address/SSID bytes preserved in SDK order");expect(aps[i].primary==source[i].primary && aps[i].rssi==source[i].rssi && aps[i].authmode==source[i].authmode,"channel/RSSI/auth are lossless");}
 for(unsigned batch=0;batch<16;++batch){serviceWifiResults();expect(submitted==4*(batch+1),"actual delivery remains four records per loop");}
 expect(bleStarts==1 && counters.scan_cycles==1,"BLE starts once after all64 APs delivered");
 for(unsigned i=0;i<64;++i){const auto& event=delivered[i];const size_t n=i==1?5:32;expect(event.ssid_length==n && event.length==n && std::memcmp(event.raw,source[i].ssid,n)==0 && std::memcmp(event.ssid_raw,source[i].ssid,n)==0,"32-byte/un-terminated and embedded-NUL SSID behavior unchanged");expect(event.channel==source[i].primary && event.rssi==source[i].rssi && std::strcmp(event.security,auth(source[i].authmode))==0,"delivered metadata equivalent to full SDK records");}
 reset(64);failPop=17;collectWifi();expect(wifiResultCount==0 && popped==16 && clears==1 && counters.radio_errors==1,"partial API failure exposes no partial inventory and clears remainder");
 reset(64);failCount=true;collectWifi();expect(wifiResultCount==0 && popped==0 && clears==1 && counters.radio_errors==1,"count failure clears SDK list and reports error");
 reset(0);collectWifi();expect(wifiResultCount==0 && popped==0 && clears==1,"empty scan clears SDK list");
 std::printf("%u AP inventory checks passed;64 records and4-delivery budget preserved; saving=2816 bytes\n",checks);
}
