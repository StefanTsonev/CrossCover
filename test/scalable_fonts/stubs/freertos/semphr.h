#pragma once
#include "FreeRTOS.h"
inline void* xSemaphoreGetMutexHolder(void*) { return nullptr; }
inline int xSemaphoreTake(void*, int) { return pdTRUE; }
inline void xSemaphoreGive(void*) {}
