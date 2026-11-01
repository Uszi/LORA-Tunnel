#pragma once
#include <ArduinoJson.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <strings.h>
#include "recon.h"
#include "text_sanitize.h"

namespace tunnel {
constexpr uint16_t kAllReconCategories=0x7ffe;
struct ReconPreferences {
  bool buttons_enabled=true,buzzer=true;
  uint8_t display_min=1,alert_min=2;
  uint16_t categories=kAllReconCategories;
};
struct Rule {
  char type[16]{},match[48]{},label[48]{};
  recon::Category category=recon::Category::Custom;
  recon::Confidence confidence=recon::Confidence::Low;
  uint16_t identifier=0;
  bool alert=false;
};
struct RuleApplication { bool applied=false,alert=true; };

inline const char* reconCategoryKey(recon::Category category) {
  static const char* keys[]={"none","alpr","flock","axon","ibeacon","ring","camera","drone","airtag","tracker","glasses","raven","skimmer","hacker","custom"};
  const unsigned n=static_cast<unsigned>(category);
  return n<sizeof(keys)/sizeof(keys[0])?keys[n]:"none";
}
inline bool parseReconCategory(const char* key,recon::Category& category) {
  if(!key)return false;
  for(unsigned i=1;i<=14;++i)if(!strcmp(key,reconCategoryKey(static_cast<recon::Category>(i)))){
    category=static_cast<recon::Category>(i);return true;
  }
  return false;
}
inline const char* reconConfidenceKey(recon::Confidence confidence) {
  return confidence==recon::Confidence::High?"high":confidence==recon::Confidence::Medium?"medium":"low";
}
inline bool parseReconConfidence(JsonVariantConst value,recon::Confidence& result) {
  if(!value.is<const char*>())return false;
  const JsonString string=value.as<JsonString>();
  if(string.size()!=strlen(string.c_str()))return false;
  if(!strcmp(string.c_str(),"low"))result=recon::Confidence::Low;
  else if(!strcmp(string.c_str(),"medium"))result=recon::Confidence::Medium;
  else if(!strcmp(string.c_str(),"high"))result=recon::Confidence::High;
  else return false;
  return true;
}
inline ReconPreferences parseReconPreferences(JsonVariantConst root) {
  ReconPreferences out;
  if(!root.is<JsonObjectConst>())return out;
  const auto ui=root["ui"];
  if(ui["buttons_enabled"].is<bool>())out.buttons_enabled=ui["buttons_enabled"].as<bool>();
  const auto recon=root["recon"];
  if(recon["buzzer"].is<bool>())out.buzzer=recon["buzzer"].as<bool>();
  tunnel::recon::Confidence confidence;
  if(parseReconConfidence(recon["display_min"],confidence))out.display_min=uint8_t(confidence);
  if(parseReconConfidence(recon["alert_min"],confidence))out.alert_min=uint8_t(confidence);
  const auto categories=recon["categories"];
  if(categories.is<JsonArrayConst>() && categories.size()<=14) {
    uint16_t mask=0;bool valid=true;
    for(JsonVariantConst entry:categories.as<JsonArrayConst>()){
      tunnel::recon::Category category;
      if(!entry.is<const char*>() || entry.as<JsonString>().size()!=strlen(entry.as<const char*>()) ||
         !parseReconCategory(entry.as<const char*>(),category)) {valid=false;break;}
      const uint16_t bit=uint16_t(1u<<unsigned(category));
      if(mask&bit){valid=false;break;}mask|=bit;
    }
    if(valid)out.categories=mask;
  }
  return out;
}
inline bool copyReconJsonText(JsonVariantConst value,char* dst,size_t capacity) {
  if(!value.is<const char*>())return false;
  const JsonString string=value.as<JsonString>();const size_t n=string.size();
  if(!n || n>=capacity || strlen(string.c_str())!=n)return false;
  return !copyUtf8Bounded(dst,capacity,reinterpret_cast<const uint8_t*>(string.c_str()),n);
}
inline int reconHex(char c) {
  return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;
}
inline bool parseReconIdentifier(const char* text,uint16_t& result) {
  if(!text)return false;
  if(text[0]=='0' && (text[1]=='x'||text[1]=='X'))text+=2;
  if(strlen(text)!=4)return false;
  unsigned value=0;
  for(unsigned i=0;i<4;++i){const int digit=reconHex(text[i]);if(digit<0)return false;value=value*16+unsigned(digit);}
  result=uint16_t(value);return true;
}
inline bool parseReconRule(JsonObjectConst entry,Rule& result) {
  Rule parsed;
  if(!copyReconJsonText(entry["type"],parsed.type,sizeof(parsed.type)) ||
     !copyReconJsonText(entry["match"],parsed.match,sizeof(parsed.match)))return false;
  if(strcmp(parsed.type,"oui") && strcmp(parsed.type,"name") && strcmp(parsed.type,"ssid_prefix") &&
     strcmp(parsed.type,"service16") && strcmp(parsed.type,"manufacturer"))return false;
  if(entry.containsKey("label")){
    if(!copyReconJsonText(entry["label"],parsed.label,sizeof(parsed.label)))return false;
  } else strcpy(parsed.label,"Custom rule match");
  if(entry.containsKey("category")){
    if(!entry["category"].is<const char*>() || entry["category"].as<JsonString>().size()!=strlen(entry["category"].as<const char*>()) ||
       !parseReconCategory(entry["category"].as<const char*>(),parsed.category))return false;
  }
  if(entry.containsKey("confidence") && !parseReconConfidence(entry["confidence"],parsed.confidence))return false;
  // Legacy entries without a category retain a custom/low interpretation.
  if(!entry.containsKey("category"))parsed.confidence=recon::Confidence::Low;
  if(entry.containsKey("alert")){
    if(!entry["alert"].is<bool>())return false;
    parsed.alert=entry["alert"].as<bool>();
  }
  if(!strcmp(parsed.type,"oui")){
    if(strlen(parsed.match)!=8)return false;
    char address[18];snprintf(address,sizeof(address),"%s:00:00:00",parsed.match);
    uint8_t bytes[6];if(!recon::parseAddress(address,bytes) || (bytes[0]&3))return false;
  } else if(!strcmp(parsed.type,"ssid_prefix")){
    if(strlen(parsed.match)>32)return false;
  } else if(!strcmp(parsed.type,"service16") || !strcmp(parsed.type,"manufacturer")){
    if(!parseReconIdentifier(parsed.match,parsed.identifier))return false;
  }
  result=parsed;return true;
}
inline bool reconAssignedUuid(const uint8_t* data,size_t width,uint16_t& result) {
  static const uint8_t base[]={0xfb,0x34,0x9b,0x5f,0x80,0,0,0x80,0,0x10,0,0};
  if(width==16){if(memcmp(data,base,12)||data[14]||data[15])return false;data+=12;}
  else if(width==4){if(data[2]||data[3])return false;}
  else if(width!=2)return false;
  result=uint16_t(data[0])|(uint16_t(data[1])<<8);return true;
}
inline bool reconAdIdentifier(const uint8_t* ad,size_t n,size_t advLength,uint16_t identifier,bool manufacturer) {
  if(!ad||!n||n>recon::kMaxBleBytes||advLength>n)return false;
  bool found=false;
  for(size_t at=0;at<n;){
    const size_t end=advLength&&at<advLength?advLength:n;
    const uint8_t length=ad[at++];if(!length){at=end;continue;}
    if(length>end-at)return false;
    const uint8_t type=ad[at];const uint8_t* data=ad+at+1;const size_t size=length-1;
    const size_t width=(type==2||type==3||type==0x16)?2:(type==4||type==5||type==0x20)?4:(type==6||type==7||type==0x21)?16:0;
    const bool serviceData=type==0x16||type==0x20||type==0x21;
    if(width && (serviceData?size<width:size%width!=0))return false;
    if(manufacturer && type==0xff && size>=2)found|=(uint16_t(data[0])|(uint16_t(data[1])<<8))==identifier;
    if(!manufacturer && width)for(size_t p=0;p+width<=size;p+=width){
      uint16_t value;if(reconAssignedUuid(data+p,width,value)&&value==identifier)found=true;
      if(serviceData)break;
    }
    at+=length;
  }
  return found;
}
inline bool reconRuleMatches(const Rule& rule,bool ble,const char* address,uint8_t addressType,
                             const char* name,const uint8_t* ad,size_t n,size_t advLength=0) {
  if(!strcmp(rule.type,"name"))return name&&!strcasecmp(name,rule.match);
  if(!strcmp(rule.type,"ssid_prefix"))return !ble&&name&&!strncasecmp(name,rule.match,strlen(rule.match));
  if(!strcmp(rule.type,"oui")){
    if(ble && addressType!=0 && addressType!=2)return false;
    uint8_t mac[6];return recon::parseAddress(address,mac)&&!(mac[0]&3)&&!strncasecmp(address,rule.match,8);
  }
  if(!ble)return false;
  if(!strcmp(rule.type,"service16"))return reconAdIdentifier(ad,n,advLength,rule.identifier,false);
  if(!strcmp(rule.type,"manufacturer"))return reconAdIdentifier(ad,n,advLength,rule.identifier,true);
  return false;
}
inline RuleApplication applyReconRuleSet(recon::Result& result,const Rule* entries,size_t count,
                                         bool ble,const char* address,uint8_t addressType,
                                         const char* name,const uint8_t* ad,size_t n,size_t advLength=0) {
  RuleApplication applied;
  if(!entries)return applied;if(count>64)count=64;
  for(size_t i=0;i<count;++i){
    const Rule& rule=entries[i];
    if(rule.confidence<=result.confidence || !reconRuleMatches(rule,ble,address,addressType,name,ad,n,advLength))continue;
    result.category=rule.category;result.confidence=rule.confidence;
    copyUtf8Bounded(result.label,sizeof(result.label),reinterpret_cast<const uint8_t*>(rule.label),strlen(rule.label));
    result.vendor[0]=0;
    snprintf(result.evidence,sizeof(result.evidence),"SD user rule %s:%s; configured evidence, role unverified",rule.type,rule.match);
    applied.applied=true;applied.alert=rule.alert;
  }
  return applied;
}
}
