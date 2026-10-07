#pragma once

#include <cstdint>

inline uint32_t utf8NextCodepoint(const unsigned char** cursor) {
  const auto* value = *cursor;
  if (*value == '\0') return 0;
  const unsigned char lead = *value++;
  if (lead < 0x80) {
    *cursor = value;
    return lead;
  }
  const int continuationCount = (lead & 0xE0) == 0xC0 ? 1 : (lead & 0xF0) == 0xE0 ? 2 : 3;
  uint32_t codepoint = lead & static_cast<uint8_t>(0x7F >> continuationCount);
  for (int i = 0; i < continuationCount; ++i) codepoint = (codepoint << 6) | (*value++ & 0x3F);
  *cursor = value;
  return codepoint;
}

inline bool utf8IsCjkBreakable(const uint32_t codepoint) {
  return (codepoint >= 0x3000 && codepoint <= 0x30FF) || (codepoint >= 0x3400 && codepoint <= 0x9FFF) ||
         (codepoint >= 0xAC00 && codepoint <= 0xD7AF);
}

inline bool utf8IsCombiningMark(const uint32_t codepoint) { return codepoint >= 0x0300 && codepoint <= 0x036F; }
