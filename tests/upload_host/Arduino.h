#pragma once
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <algorithm>
extern uint32_t hostMillis;
inline uint32_t millis(){return hostMillis;}
inline size_t strlcpy(char* out,const char* source,size_t cap){size_t n=std::strlen(source);if(cap){size_t copy=std::min(n,cap-1);std::memcpy(out,source,copy);out[copy]=0;}return n;}
using portMUX_TYPE=int;constexpr int portMUX_INITIALIZER_UNLOCKED=0,portMAX_DELAY=0,pdPASS=1;
inline void portENTER_CRITICAL(portMUX_TYPE*){}
inline void portEXIT_CRITICAL(portMUX_TYPE*){}
inline void xSemaphoreTake(int,int){}
inline void xSemaphoreGive(int){}
inline uint32_t pdMS_TO_TICKS(uint32_t n){return n;}
inline void vTaskDelay(uint32_t n){hostMillis+=n;}
inline void taskYIELD(){}
struct HostTaskFinished{};
inline void vTaskDelete(void*){throw HostTaskFinished{};}
namespace host {extern void(*task)(void*);extern void* taskArg;}
inline int xTaskCreatePinnedToCore(void(*fn)(void*),const char*,int,void* arg,int,void*,int){host::task=fn;host::taskArg=arg;return pdPASS;}
