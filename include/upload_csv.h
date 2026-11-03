#pragma once
#include <ArduinoJson.h>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace tunnel { namespace upload {
constexpr size_t CsvRowCapacity=2048;
constexpr size_t CsvChunkLimit=1024U*1024U;
constexpr char CsvColumns[]="MAC,SSID,AuthMode,FirstSeen,Channel,Frequency,RSSI,CurrentLatitude,CurrentLongitude,AltitudeMeters,AccuracyMeters,RCOIs,MfgrId,Type\n";
inline bool number(JsonVariantConst value,double low,double high) {
  if(value.is<bool>() || (!value.is<double>() && !value.is<long>() && !value.is<unsigned long>()))return false;
  const double n=value.as<double>();return std::isfinite(n) && n>=low && n<=high;
}
inline bool integer(JsonVariantConst value,unsigned low,unsigned high) {
  return !value.is<bool>() && value.is<unsigned long>() && value.as<unsigned long>()>=low && value.as<unsigned long>()<=high;
}
inline bool text(JsonVariantConst value,const char*& result,size_t limit) {
  if(value.isNull()) {result="";return true;}
  if(!value.is<const char*>())return false;
  const JsonString string=value.as<JsonString>();result=string.c_str();
  return string.size()<=limit && string.size()==std::strlen(result);
}
inline int hexDigit(char c) {return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;}
inline bool macAddress(const char* address) {
  if(!address || std::strlen(address)!=17)return false;
  for(unsigned i=0;i<17;++i)if(i%3==2?address[i]!=':':hexDigit(address[i])<0)return false;
  return true;
}
inline bool validUtf8(const uint8_t* bytes,size_t length) {
  for(size_t i=0;i<length;) {
    const uint8_t first=bytes[i++];if(!first)return false;if(first<128)continue;
    unsigned count;uint32_t point,minimum;
    if(first>=0xc2 && first<=0xdf){count=1;point=first&31;minimum=0x80;}
    else if(first>=0xe0 && first<=0xef){count=2;point=first&15;minimum=0x800;}
    else if(first>=0xf0 && first<=0xf4){count=3;point=first&7;minimum=0x10000;}
    else return false;
    if(count>length-i)return false;
    while(count--){const uint8_t c=bytes[i++];if((c&0xc0)!=0x80)return false;point=(point<<6)|(c&63);}
    if(point<minimum || point>0x10ffff || (point>=0xd800 && point<=0xdfff))return false;
  }
  return true;
}
inline bool leap(unsigned year){return year%4==0 && (year%100!=0 || year%400==0);}
// Native JSONL UTC is emitted in this exact form. Never invent wall time from
// monotonic_ms, stale timestamps, locale, or an unqualified date string.
inline bool nativeUtc(const char* utc,char result[20]) {
  if(!utc || std::strlen(utc)!=20 || utc[4]!='-' || utc[7]!='-' || utc[10]!='T' || utc[13]!=':' || utc[16]!=':' || utc[19]!='Z')return false;
  for(unsigned i=0;i<19;++i)if(i!=4&&i!=7&&i!=10&&i!=13&&i!=16&&(utc[i]<'0'||utc[i]>'9'))return false;
  const unsigned year=(utc[0]-'0')*1000+(utc[1]-'0')*100+(utc[2]-'0')*10+utc[3]-'0';
  const unsigned month=(utc[5]-'0')*10+utc[6]-'0', day=(utc[8]-'0')*10+utc[9]-'0';
  const unsigned hour=(utc[11]-'0')*10+utc[12]-'0',minute=(utc[14]-'0')*10+utc[15]-'0',second=(utc[17]-'0')*10+utc[18]-'0';
  const unsigned days[]={31,28,31,30,31,30,31,31,30,31,30,31};
  if(year<2020||month<1||month>12||day<1||day>days[month-1]+(month==2&&leap(year)?1:0)||hour>23||minute>59||second>59)return false;
  std::memcpy(result,utc,19);result[10]=' ';result[19]=0;return true;
}
class CsvLine {
  char* out_;size_t capacity_,used_=0;bool ok_=true,first_=true;
  void character(char c){if(used_+1>=capacity_)ok_=false;else out_[used_++]=c;}
public:
  CsvLine(char* out,size_t capacity):out_(out),capacity_(capacity){if(capacity)out[0]=0;}
  void field(const char* value) {
    if(!first_)character(',');first_=false;character('"');
    for(const char* p=value;*p;++p){if(*p=='"')character('"');if(*p=='\r'||*p=='\n'){character('\\');character(*p=='\r'?'r':'n');}else character(*p);}
    character('"');
  }
  void numeric(JsonVariantConst value){char numberText[40];std::snprintf(numberText,sizeof(numberText),"%.10g",value.as<double>());field(numberText);}
  size_t finish(){character('\n');if(capacity_)out_[used_<capacity_?used_:capacity_-1]=0;return ok_?used_:0;}
};
inline bool wardriverMode(JsonVariantConst event) {
  return !event.containsKey("capture_mode") || (event["capture_mode"].is<const char*>() && !std::strcmp(event["capture_mode"].as<const char*>(),"wardriver"));
}
inline bool synthetic(JsonVariantConst event) {return event["synthetic"].is<bool>()&&event["synthetic"].as<bool>();}
inline size_t csvRow(JsonVariantConst event,char* output,size_t capacity) {
  if(!integer(event["schema"],1,1) || !wardriverMode(event) || synthetic(event))return 0;
  const char* kind=event["type"]|"";const bool wifi=!std::strcmp(kind,"wifi"),ble=!std::strcmp(kind,"ble");
  if(!wifi && !ble)return 0;
  const auto radio=event[kind],gps=event["gps"];
  if(!radio.is<JsonObjectConst>() || !gps.is<JsonObjectConst>() || !gps["valid"].is<bool>() || !gps["valid"].as<bool>() ||
      !number(gps["age_ms"],0,10000) || !number(gps["lat"],-90,90) || !number(gps["lon"],-180,180))return 0;
  const auto rawFlag=[](JsonVariantConst object){return object.containsKey("wifi_frame")&&(!object["wifi_frame"].is<bool>()||object["wifi_frame"].as<bool>());};
  if(wifi && (rawFlag(event)||rawFlag(radio)||radio.containsKey("raw_frame_hex")))return 0;
  char stamp[20];if(!nativeUtc(event["utc"],stamp))return 0;
  const char* address=radio[wifi?"bssid":"address"]|"";const char* name;const char* security="[BLE]";
  if(!macAddress(address)||!text(radio[wifi?"ssid":"name"],name,512)||!number(radio["rssi"],-127,20))return 0;
  char restored[33]{};
  if(wifi) {
    if(!integer(radio["channel"],1,14)||!number(radio["frequency_mhz"],2400,2500)||!text(radio["security"],security,256))return 0;
    const unsigned channel=radio["channel"].as<unsigned>();
    if(radio["frequency_mhz"].as<double>()!=(channel==14?2484:2407+5*channel))return 0;
    if(radio["ssid_sanitized"].is<bool>() && radio["ssid_sanitized"].as<bool>()) {
      name="";const char* bytes=radio["ssid_hex"]|"";const size_t n=std::strlen(bytes);bool valid=n<=64&&n%2==0;
      for(size_t i=0;valid&&i<n;i+=2){const int a=hexDigit(bytes[i]),b=hexDigit(bytes[i+1]);if(a<0||b<0)valid=false;else restored[i/2]=char((a<<4)|b);}
      if(valid&&validUtf8(reinterpret_cast<uint8_t*>(restored),n/2))name=restored;
    }
  } else {
    const char* type=radio["address_type"]|"";
    if(std::strcmp(type,"public") && std::strcmp(type,"public_identity"))return 0;
    if(!radio["manufacturer_id"].isNull() && !integer(radio["manufacturer_id"],0,65535))return 0;
    if(radio["name_sanitized"].is<bool>() && radio["name_sanitized"].as<bool>())name="";
  }
  char mac[18];for(unsigned i=0;i<18;++i)mac[i]=(address[i]>='a'&&address[i]<='f')?char(address[i]-32):address[i];
  CsvLine row(output,capacity);row.field(mac);row.field(name);row.field(security);row.field(stamp);
  if(wifi){row.numeric(radio["channel"]);row.numeric(radio["frequency_mhz"]);}else{row.field("");row.field("");}
  row.numeric(radio["rssi"]);row.numeric(gps["lat"]);row.numeric(gps["lon"]);
  if(number(gps["alt_m"],-1000,100000))row.numeric(gps["alt_m"]);else row.field("");
  if(number(gps["accuracy_m"],0,100000))row.numeric(gps["accuracy_m"]);else row.field("");
  row.field("");if(ble&&!radio["manufacturer_id"].isNull())row.numeric(radio["manufacturer_id"]);else row.field("");
  row.field(wifi?"WIFI":"BLE");return row.finish();
}
} }
