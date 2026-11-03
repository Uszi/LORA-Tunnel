#pragma once
#include <Arduino.h>
// Deterministic host stand-in exercises source identity changes, not SHA256's
// cryptographic implementation. Embedded builds use the SDK mbedTLS primitive.
struct mbedtls_sha256_context{uint32_t state=2166136261U;};
inline void mbedtls_sha256_init(mbedtls_sha256_context* c){c->state=2166136261U;}
inline int mbedtls_sha256_starts(mbedtls_sha256_context*,int){return 0;}
inline int mbedtls_sha256_update(mbedtls_sha256_context* c,const uint8_t* p,size_t n){while(n--)c->state=(c->state^*p++)*16777619U;return 0;}
inline int mbedtls_sha256_finish(mbedtls_sha256_context* c,uint8_t* out){for(unsigned i=0;i<32;++i)out[i]=uint8_t((c->state>>(8*(i%4)))^i);return 0;}
inline void mbedtls_sha256_free(mbedtls_sha256_context*){}
