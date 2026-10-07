#pragma once

#include <cstddef>
#include <cstdint>

// Format a known date using translated month names and the persisted date-format values.
bool formatDateParts(char* buf, size_t bufSize, uint16_t year, uint8_t month, uint8_t day, uint8_t dateFormat,
                     char numericSeparator = '/');
