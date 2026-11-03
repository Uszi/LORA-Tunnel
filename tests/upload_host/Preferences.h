#pragma once
#include <Arduino.h>
namespace host {extern uint32_t epoch;}
struct Preferences {bool begin(const char*,bool){return true;}uint32_t getUInt(const char*,uint32_t fallback){return host::epoch?host::epoch:fallback;}size_t putUInt(const char*,uint32_t value){host::epoch=value;return sizeof(value);}void end(){}};
