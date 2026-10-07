#pragma once

#include <EpdFontFamily.h>

#include <cstring>
#include <deque>
#include <string>
#include <vector>

namespace BidiUtils {
enum class BidiBaseDir : signed char { AUTO = -1, LTR = 0, RTL = 1 };
}

class GfxRenderer {
 public:
  int textAdvancePerChar = 0;
  uint8_t scalableBaseSize = 0;
  uint8_t getFontPointSize(int id) const { return scalableBaseSize ? (id ? id : scalableBaseSize) : 0; }
  int getFontIdForSize(int id, uint8_t size) const { return scalableBaseSize && size ? size : id; }
  int getFontAscenderSize(int id) const { return scalableBaseSize ? getFontPointSize(id) : 12; }
  int getLineHeight(int id) const { return scalableBaseSize ? getFontPointSize(id) * 2 : 16; }
  int getTextWidth(int, const char*, EpdFontFamily::Style = EpdFontFamily::REGULAR) const { return 0; }
  int getTextAdvanceX(int id, const char* text, EpdFontFamily::Style, uint32_t = 0) const {
    return static_cast<int>(std::strlen(text)) * textAdvancePerChar * (scalableBaseSize ? getFontPointSize(id) : 1) /
           (scalableBaseSize ? scalableBaseSize : 1);
  }
  int getSpaceWidth(int, EpdFontFamily::Style) const { return 0; }
  int getKerning(int, uint32_t, uint32_t, EpdFontFamily::Style) const { return 0; }
  int getSpaceAdvance(int, uint32_t, uint32_t, EpdFontFamily::Style) const { return 0; }
  bool isSdCardFont(int) const { return false; }
  bool releaseSdCardFontForLowMemory(int, bool = false) { return false; }
  bool ensureSdCardFontReady(int, const uint32_t*, size_t, bool, bool, uint8_t) const { return true; }
  bool ensureSdCardFontReady(int, const std::deque<std::string>&, bool, uint8_t) const { return true; }
  bool ensureSdCardFontReady(int, const char*, uint8_t) const { return true; }
  std::vector<std::string> wrappedText(int, const char*, int, int,
                                       EpdFontFamily::Style = EpdFontFamily::REGULAR) const {
    return {};
  }
};
