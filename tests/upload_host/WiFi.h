#pragma once
#include <Arduino.h>
constexpr int WIFI_STA=1,WL_CONNECTED=3;
namespace host {extern bool connected;extern unsigned associations;extern bool bleReady,disconnected;}
struct WiFiClass {
  void persistent(bool){}void setAutoReconnect(bool){}void mode(int){}
  void begin(const char*,const char*){
#if defined(LORA_TUNNEL_CARDPUTER_ADV)
    if(host::bleReady)throw std::runtime_error("Wi-Fi associated before BLE release");
#endif
    host::disconnected=false;++host::associations;
  }
  int status(){return host::connected?WL_CONNECTED:0;}
  void disconnect(bool,bool){host::disconnected=true;}
};
extern WiFiClass WiFi;
inline void configTime(int,int,const char*,const char*){}
