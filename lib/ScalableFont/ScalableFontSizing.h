#pragma once

#include <cstdint>

// Content may be larger than the reader's selectable 8-22 pt body size.
constexpr uint8_t ScalableContentMinPointSize = 8;
constexpr uint8_t ScalableContentMaxPointSize = 44;

// Reader size labels predate scalable fonts and are calibrated to the 150-DPI
// .cpfont asset pipeline. Keep TTF rendering on that established visual scale
// so selecting the same size does not unexpectedly enlarge the book text.
constexpr uint32_t ScalableFontCompatibilityPpi = 150;

constexpr uint32_t scalableFontPixelSize26_6ForPpi(const uint8_t points, const uint32_t ppi) {
  return (uint32_t(points) * ppi * 64 + 36) / 72;
}

// CrossInk's reader-size convention is firmware policy. The SDK accepts generic
// 26.6 pixel sizes and has no knowledge of labels or display density.
constexpr uint32_t scalableFontPixelSize26_6(const uint8_t points) {
  return scalableFontPixelSize26_6ForPpi(points, ScalableFontCompatibilityPpi);
}
