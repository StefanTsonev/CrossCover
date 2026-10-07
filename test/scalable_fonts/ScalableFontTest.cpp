#include <FtFont.h>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "../../lib/ScalableFont/FixedArenaAllocator.h"
#include "../../lib/ScalableFont/ScalableFontSizing.h"

using freeink::font::FtFont;

namespace {

std::vector<uint8_t> readFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  assert(file.good());
  return {std::istreambuf_iterator<char>(file), {}};
}

uint32_t pointSize26_6(const unsigned points, const unsigned ppi) { return (points * ppi * 64u + 36u) / 72u; }

struct Source {
  const std::vector<uint8_t>* bytes;
};

unsigned long readAt(void* context, const unsigned long offset, unsigned char* buffer, const unsigned long count) {
  const auto& bytes = *static_cast<Source*>(context)->bytes;
  if (offset > bytes.size() || count > bytes.size() - offset) return 0;
  if (count) std::memcpy(buffer, bytes.data() + offset, count);
  return count;
}

}  // namespace

int main(int argc, char** argv) {
  assert(argc == 2);
  static constexpr size_t FontArenaBytes = 1280 * 1024;
  alignas(std::max_align_t) static std::array<uint8_t, FontArenaBytes> arenaStorage{};
  static FixedArenaAllocator arena;
  assert(arena.initialize(arenaStorage.data(), arenaStorage.size()));
  assert(arena.freeBytes() == arena.largestFreeBlock());
  assert(FixedArenaAllocator::allocate(&arena, FontArenaBytes) == nullptr);
  assert(arena.lastFailedRequest() == FontArenaBytes);
  arena.clearFailure();
  assert(arena.lastFailedRequest() == 0);
  static_assert(scalableFontPixelSize26_6ForPpi(12, 219) == 2336);
  static_assert(scalableFontPixelSize26_6ForPpi(12, 235) == 2507);
  static_assert(scalableFontPixelSize26_6ForPpi(12, 259) == 2763);
  static_assert(ScalableFontCompatibilityPpi == 150);
  static_assert(scalableFontPixelSize26_6(12) == 1600);
  void* grown = FixedArenaAllocator::allocate(&arena, 64);
  assert(grown);
  grown = FixedArenaAllocator::reallocate(&arena, grown, 64, 128);
  assert(grown);
  void* retainedTail = FixedArenaAllocator::allocate(&arena, 64);
  assert(retainedTail);
  FixedArenaAllocator::deallocate(&arena, retainedTail);
  FixedArenaAllocator::deallocate(&arena, grown);
  assert(arena.initialize(arenaStorage.data(), arenaStorage.size()));
  const FtFont::MemoryCallbacks callbacks{&arena, FixedArenaAllocator::allocate, FixedArenaAllocator::deallocate,
                                          FixedArenaAllocator::reallocate};
  assert(FtFont::configureMemory(&callbacks));
  const char* names[] = {"bitter_regular",     "bitter_bold",     "bitter_italic",     "bitter_bolditalic",
                         "lexenddeca_regular", "lexenddeca_bold", "lexenddeca_italic", "lexenddeca_bolditalic"};
  constexpr unsigned panelPpis[] = {219, 235, 259};

  for (const char* name : names) {
    const auto data = readFile(std::string(argv[1]) + "/" + name + ".ttf");
    FtFont::FaceInfo memoryInfo;
    char family[128];
    assert(FtFont::inspectMemory(data.data(), static_cast<uint32_t>(data.size()), memoryInfo, family, sizeof(family)) ==
           FtFont::InspectResult::Ok);
    assert(family[0]);

    Source source{&data};
    FtFont::FaceInfo streamInfo;
    char streamedFamily[128];
    assert(FtFont::inspectStream(readAt, &source, data.size(), streamInfo, streamedFamily, sizeof(streamedFamily)) ==
           FtFont::InspectResult::Ok);
    assert(std::strcmp(family, streamedFamily) == 0);
    assert(memoryInfo.weight == streamInfo.weight && memoryInfo.italic == streamInfo.italic);

    FtFont font;
    assert(font.init(data.data(), static_cast<uint32_t>(data.size()), 1));
    FtFont::RenderOptions options;
    options.hinting = FtFont::HintingMode::Auto;
    assert(font.setRenderOptions(options));

    const auto glyphA = font.glyphId('A');
    assert(glyphA);
    for (const unsigned ppi : panelPpis) {
      const uint32_t size26_6 = pointSize26_6(12, ppi);
      FtFont::GlyphMetrics metrics;
      FtFont::LineMetrics line;
      assert(font.metricsGlyph26_6(glyphA, size26_6, metrics));
      assert(metrics.advance26_6 > 0 && metrics.width && metrics.height);
      assert(font.lineMetrics26_6(size26_6, line));
      assert(line.height26_6 > 0);
      const auto* bitmap = font.rasterizeGlyph26_6(glyphA, size26_6);
      assert(bitmap && bitmap->width == metrics.width && bitmap->height == metrics.height);
      font.kerningGlyphs26_6(glyphA, font.glyphId('V'), size26_6);
    }

    static constexpr uint32_t sequences[5][3] = {
        {'f', 'f', 0}, {'f', 'i', 0}, {'f', 'l', 0}, {'f', 'f', 'i'}, {'f', 'f', 'l'}};
    for (unsigned i = 0; i < 5; ++i) {
      const unsigned length = sequences[i][2] ? 3 : 2;
      const auto compatibilityGlyph = font.glyphId(0xfb00 + i);
      const auto glyph = compatibilityGlyph ? compatibilityGlyph : font.ligatureGlyphId(sequences[i], length);
      assert(glyph);
      FtFont::GlyphMetrics metrics;
      assert(font.metricsGlyph26_6(glyph, pointSize26_6(12, 235), metrics));
      assert(font.rasterizeGlyph26_6(glyph, pointSize26_6(12, 235)));
    }

    options.embolden26_6 = 32;
    options.slant16_16 = 9209;
    assert(font.setRenderOptions(options));
    assert(font.rasterize26_6('A', pointSize26_6(12, 219)));

    font.deinit();
    FtFont streamed;
    assert(streamed.initStream(readAt, &source, data.size(), 1));
    options = FtFont::RenderOptions{};
    options.hinting = FtFont::HintingMode::Auto;
    assert(streamed.setRenderOptions(options));
    FtFont::GlyphMetrics streamedMetrics;
    assert(streamed.metrics26_6('A', pointSize26_6(12, 235), streamedMetrics));
    assert(streamedMetrics.advance26_6 > 0);
    assert(streamed.rasterize26_6('A', pointSize26_6(12, 235)));
    std::printf("%s: metadata, 219/235/259 PPI metrics, glyph IDs, GSUB, and tuning passed\n", name);
  }

  std::array<std::vector<uint8_t>, 4> familyData;
  std::array<FtFont, 4> family;
  for (size_t style = 0; style < family.size(); ++style) {
    familyData[style] = readFile(std::string(argv[1]) + "/" + names[style] + ".ttf");
    assert(family[style].init(familyData[style].data(), static_cast<uint32_t>(familyData[style].size()), 1));
    FtFont::RenderOptions options;
    options.hinting = FtFont::HintingMode::Auto;
    assert(family[style].setRenderOptions(options));
  }
  for (unsigned points = 8; points <= 22; ++points) {
    for (auto& font : family) {
      for (uint32_t cp = 0x20; cp < 0x500; ++cp) {
        const auto glyph = font.glyphId(cp);
        if (!glyph) continue;
        FtFont::GlyphMetrics metrics;
        assert(font.metricsGlyph26_6(glyph, scalableFontPixelSize26_6(points), metrics));
        const auto* bitmap = font.rasterizeGlyph26_6(glyph, scalableFontPixelSize26_6(points));
        assert(bitmap && bitmap->width == metrics.width && bitmap->height == metrics.height);
      }
    }
  }

  return 0;
}
