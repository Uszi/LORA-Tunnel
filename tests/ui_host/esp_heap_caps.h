#pragma once
#include <cstdlib>
constexpr int MALLOC_CAP_INTERNAL=1, MALLOC_CAP_8BIT=2;
static size_t hostAllocationBytes=0, hostAllocationCount=0;
inline void* heap_caps_malloc(size_t size,int) {
  void* result=std::malloc(size);
  if(result) {hostAllocationBytes+=size;++hostAllocationCount;}
  return result;
}
