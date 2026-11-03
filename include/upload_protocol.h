#pragma once
#include <ArduinoJson.h>
#include <cstdint>
#include <cstring>
#include <cstdio>
namespace tunnel { namespace upload {
enum class Receipt {Rejected,Accepted,JobPending,JobFailed,Authentication,Retryable};
inline bool safeJobId(const char* id) {
  if(!id || !*id || std::strlen(id)>80)return false;
  for(const char* p=id;*p;++p)if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='-'||*p=='_'))return false;
  return true;
}
inline bool normalizeJobId(JsonVariantConst value,char* output,size_t capacity) {
  if(value.is<const char*>()) {
    const JsonString text=value.as<JsonString>();
    if(text.size()!=std::strlen(text.c_str())||!safeJobId(text.c_str())||text.size()>=capacity)return false;
    std::memcpy(output,text.c_str(),text.size()+1);return true;
  }
  if(value.is<bool>()||!value.is<uint64_t>()||value.as<uint64_t>()==0)return false;
  const int n=std::snprintf(output,capacity,"%llu",static_cast<unsigned long long>(value.as<uint64_t>()));
  return n>0&&size_t(n)<capacity;
}
inline Receipt receipt(int service,int status,JsonVariantConst body,bool polling=false) {
  if(status==401 || status==403)return Receipt::Authentication;
  if(status==429 || status>=500 || status<=0)return Receipt::Retryable;
  if(!body.is<JsonObjectConst>())return Receipt::Rejected;
  if(service==0)return status==200 && body["success"].is<bool>() && body["success"].as<bool>()?Receipt::Accepted:Receipt::Rejected;
  if(service==1) {
    const bool accepted=body["accepted"].is<bool>() && body["accepted"].as<bool>();
    const bool duplicate=body["duplicate"].is<bool>() && body["duplicate"].as<bool>();
    return status==200 && (accepted||duplicate)?Receipt::Accepted:Receipt::Rejected;
  }
  if(service!=2)return Receipt::Rejected;
  if(!polling){char job[81];return status==202 && normalizeJobId(body["job_id"],job,sizeof(job))?Receipt::JobPending:Receipt::Rejected;}
  if(status!=200)return Receipt::Rejected;
  const char* state=body["status"]|"";
  if(!std::strcmp(state,"done"))return Receipt::Accepted;
  if(!std::strcmp(state,"failed"))return Receipt::JobFailed;
  if(!std::strcmp(state,"pending")||!std::strcmp(state,"queued")||!std::strcmp(state,"processing")||!std::strcmp(state,"running"))return Receipt::JobPending;
  return Receipt::Rejected;
}
inline uint32_t retryDelay(unsigned attempts){return attempts<=1?30000:attempts==2?120000:600000;}
// An on-disk sending checkpoint without a receipt is ambiguous after reboot.
// Automatic work never resends it. An explicit manual retry permits another try.
inline bool checkpointMaySend(const char* state,bool manual) {
  if(!std::strcmp(state,"accepted")||!std::strcmp(state,"skipped")||!std::strcmp(state,"job"))return false;
  return manual || (std::strcmp(state,"sending") && std::strcmp(state,"uncertain") && std::strcmp(state,"rejected"));
}
} }
