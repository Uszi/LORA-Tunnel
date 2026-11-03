#pragma once
#include <Arduino.h>
inline int mbedtls_base64_encode(uint8_t* out,size_t cap,size_t* written,const uint8_t* in,size_t n){
  const char* alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";size_t used=0;
  for(size_t i=0;i<n;i+=3){uint32_t v=uint32_t(in[i])<<16;if(i+1<n)v|=uint32_t(in[i+1])<<8;if(i+2<n)v|=in[i+2];if(used+4>cap)return -1;out[used++]=alphabet[(v>>18)&63];out[used++]=alphabet[(v>>12)&63];out[used++]=i+1<n?alphabet[(v>>6)&63]:'=';out[used++]=i+2<n?alphabet[v&63]:'=';}*written=used;return 0;
}
