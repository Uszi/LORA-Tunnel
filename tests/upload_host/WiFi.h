#pragma once
#include <Arduino.h>
constexpr int WIFI_STA=1,WL_CONNECTED=3;
namespace host {extern bool connected;extern unsigned associations;}
struct WiFiClass {
  void persistent(bool){}void setAutoReconnect(bool){}void mode(int){}
  void begin(const char*,const char*){++host::associations;}
  int status(){return host::connected?WL_CONNECTED:0;}
  void disconnect(bool,bool){}
};
extern WiFiClass WiFi;
inline void configTime(int,int,const char*,const char*){}
