#include <TunnelMeshDecode.h>
#include "mesh_signature.h"
#include <mbedtls/aes.h>
#include <mbedtls/platform_util.h>
#include <Ed25519.h>
#include <cstring>

namespace tunnel { namespace mesh {
namespace {
bool ctr(const uint8_t* key,size_t keyLength,const uint8_t iv[16],
         const uint8_t* input,size_t length,uint8_t* output) {
  if(!key || !iv || !input || !output || (keyLength!=16 && keyLength!=32) || length>239)return false;
  mbedtls_aes_context ctx;
  mbedtls_aes_init(&ctx);
  uint8_t nonce[16],stream[16]{}; size_t offset=0;
  memcpy(nonce,iv,16);
  const bool ok=mbedtls_aes_setkey_enc(&ctx,key,unsigned(keyLength*8))==0 &&
      mbedtls_aes_crypt_ctr(&ctx,length,&offset,nonce,stream,input,output)==0;
  mbedtls_platform_zeroize(nonce,sizeof(nonce));
  mbedtls_platform_zeroize(stream,sizeof(stream));
  mbedtls_aes_free(&ctx);
  if(!ok)mbedtls_platform_zeroize(output,length);
  return ok;
}
bool verify(const uint8_t key[32],const uint8_t signature[64],const uint8_t* message,size_t length) {
  if(!key || !signature || !message || length>239)return false;
  if(!ed25519InputsAllowed(key,signature))return false;
  return Ed25519::verify(signature,key,message,length);
}
}
const Crypto& platformCrypto() {
  static const Crypto crypto{ctr,verify}; return crypto;
}
} }
