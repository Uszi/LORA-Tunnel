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
#include <set>
#include <stdexcept>
#include "../host_string.h"
extern uint32_t hostMillis;
inline uint32_t millis(){return hostMillis;}
using portMUX_TYPE=int;constexpr int portMUX_INITIALIZER_UNLOCKED=0,portMAX_DELAY=0,pdPASS=1;
inline void portENTER_CRITICAL(portMUX_TYPE*){}
inline void portEXIT_CRITICAL(portMUX_TYPE*){}
void xSemaphoreTake(int,int);
void xSemaphoreGive(int);
inline uint32_t pdMS_TO_TICKS(uint32_t n){return n;}
using TaskHandle_t=void*;
enum eTaskState {eRunning,eReady,eSuspended};
void vTaskDelay(uint32_t n);
inline void taskYIELD(){}
struct HostTaskFinished{};
struct HostTaskSuspended{};
void vTaskDelete(void* task);
void vTaskSuspend(void* task);
eTaskState eTaskGetState(TaskHandle_t task);
TaskHandle_t xTaskGetCurrentTaskHandle();
int xTaskCreatePinnedToCore(void(*fn)(void*),const char*,int,void* arg,int,TaskHandle_t*,int);

inline uint32_t uxTaskGetStackHighWaterMark(TaskHandle_t){return 4096;}
