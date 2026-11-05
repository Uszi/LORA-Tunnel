#pragma once
#include <ArduinoJson.h>
#include <cstring>
#include "mesh_decode.h"

namespace tunnel {
inline int meshHex(char c) {
  return c>='0' && c<='9'?c-'0':c>='a' && c<='f'?c-'a'+10:c>='A' && c<='F'?c-'A'+10:-1;
}
inline bool parseMeshKeys(JsonVariantConst radio,mesh::Key* keys,uint8_t& count,bool& publicDefault) {
  count=0; publicDefault=true;
  if(radio.containsKey("decode_public")) {
    if(!radio["decode_public"].is<bool>())return false;
    publicDefault=radio["decode_public"].as<bool>();
  }
  if(!radio.containsKey("meshtastic_keys"))return true;
  if(!radio["meshtastic_keys"].is<JsonArrayConst>())return false;
  const JsonArrayConst entries=radio["meshtastic_keys"];
  if(entries.size()>mesh::MaxKeys)return false;
  for(JsonVariantConst entry:entries) {
    if(!entry.is<JsonObjectConst>() || !entry["name"].is<const char*>() || !entry["key_hex"].is<const char*>())return false;
    const JsonString name=entry["name"].as<JsonString>(), hex=entry["key_hex"].as<JsonString>();
    if(!name.size() || name.size()>39 || std::strlen(name.c_str())!=name.size() ||
       std::strlen(hex.c_str())!=hex.size() || (hex.size()!=0 && hex.size()!=32 && hex.size()!=64))return false;
    mesh::Key key{}; key.length=hex.size()/2;
    for(size_t i=0;i<name.size();++i)key.channel_hash^=uint8_t(name.c_str()[i]);
    for(size_t i=0;i<key.length;++i) {
      const int a=meshHex(hex.c_str()[i*2]),b=meshHex(hex.c_str()[i*2+1]);
      if(a<0 || b<0)return false;
      key.bytes[i]=uint8_t(a*16+b); key.channel_hash^=key.bytes[i];
    }
    if(keys)keys[count]=key;
    ++count;
  }
  return true;
}
inline const char* loraModeName(uint8_t mode) {
  return mode==1?"meshcore":mode==2?"combined":"meshtastic";
}
}
