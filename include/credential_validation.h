#pragma once
#include <cstddef>
#include <cstring>

namespace tunnel {
inline bool validCredential(int field,const char* value) {
  if(field<0 || field>5 || !value) return false;
  const size_t n=std::strlen(value);
  if(n>127) return false;
  for(size_t i=0;i<n;++i) {
    const auto c=static_cast<unsigned char>(value[i]);
    if(c<32 || c==127) return false; // Never put control bytes in HTTP headers.
  }
  if(field==0 && std::strchr(value,':')) return false; // HTTP Basic name separator.
  if(field==3) return n<=32; // ESP32 SSID is at most32 bytes.
  if(field==4) {
    if(n==0 || (n>=8 && n<=63)) return true;
    if(n!=64) return false;
    for(size_t i=0;i<n;++i)
      if(!((value[i]>='0' && value[i]<='9') || (value[i]>='a' && value[i]<='f') || (value[i]>='A' && value[i]<='F'))) return false;
  }
  return true;
}
}
