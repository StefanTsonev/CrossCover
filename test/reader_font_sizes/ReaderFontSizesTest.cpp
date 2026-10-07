#include <cassert>
#include <iterator>

#include "../../src/ReaderFontSizeStep.h"
#include "../../src/util/FontFamilyLabel.h"

int main() {
#if CROSSINK_SCALABLE_FONTS
  assert(fontFamilyLabel("Bitter", BUILTIN_FONT_POINT_SIZE_RANGE) == "Bitter (8-22pt)");
  assert(std::size(BUILTIN_READER_FONT_SIZES) == 15);
  for (uint8_t points = 8; points <= 22; ++points) {
    assert(closestBuiltinReaderPointSize(points) == points);
    uint8_t selected = points;
    const bool changed = changeReaderFontSizeStep(BUILTIN_READER_FONT_SIZES, std::size(BUILTIN_READER_FONT_SIZES),
                                                  selected, true, FontSizeStepMode::Clamp);
    assert(changed == (points < 22));
    assert(selected == (points < 22 ? points + 1 : 22));
  }
  assert(closestBuiltinReaderPointSize(0) == 8);
  assert(closestBuiltinReaderPointSize(255) == 22);
#else
  assert(fontFamilyLabel("Bitter", BUILTIN_FONT_POINT_SIZE_RANGE) == "Bitter (10-16pt)");
  assert(std::size(BUILTIN_READER_FONT_SIZES) == 4);
  assert(closestBuiltinReaderPointSize(8) == 10);
  assert(closestBuiltinReaderPointSize(15) == 14);
  assert(closestBuiltinReaderPointSize(22) == 16);
#endif
  uint8_t selected = BUILTIN_READER_FONT_SIZES[0];
  assert(!changeReaderFontSizeStep(BUILTIN_READER_FONT_SIZES, std::size(BUILTIN_READER_FONT_SIZES), selected, false,
                                   FontSizeStepMode::Clamp));
  assert(changeReaderFontSizeStep(BUILTIN_READER_FONT_SIZES, std::size(BUILTIN_READER_FONT_SIZES), selected, false));
  assert(selected == BUILTIN_READER_FONT_SIZES[std::size(BUILTIN_READER_FONT_SIZES) - 1]);
}
