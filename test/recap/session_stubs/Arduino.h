#pragma once
#include <cstdint>
inline uint32_t sessionTicks = 0;
inline uint32_t millis() { return sessionTicks; }
inline void delay(unsigned ms) { sessionTicks += ms; }
struct SessionESP {
  unsigned getFreeHeap() { return 90000; }
  unsigned getMaxAllocHeap() { return 70000; }
};
inline SessionESP ESP;
