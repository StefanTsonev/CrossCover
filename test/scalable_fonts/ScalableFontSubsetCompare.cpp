#include <FtFont.h>

#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
using freeink::font::FtFont;
std::vector<uint8_t> load(const std::string& p) {
  std::ifstream f(p, std::ios::binary);
  assert(f.good());
  return {std::istreambuf_iterator<char>(f), {}};
}
int main(int argc, char** argv) {
  assert(argc == 3);
  const char* names[] = {"bitter_regular",     "bitter_bold",     "bitter_italic",     "bitter_bolditalic",
                         "lexenddeca_regular", "lexenddeca_bold", "lexenddeca_italic", "lexenddeca_bolditalic"};
  size_t count = 0;
  for (auto name : names) {
    auto a = load(std::string(argv[1]) + "/" + name + ".ttf"), b = load(std::string(argv[2]) + "/" + name + ".ttf");
    FtFont x, y;
    assert(x.init(a.data(), a.size(), 1));
    assert(y.init(b.data(), b.size(), 1));
    for (auto hint : {FtFont::HintingMode::Default, FtFont::HintingMode::Auto, FtFont::HintingMode::None}) {
      FtFont::RenderOptions o;
      o.hinting = hint;
      assert(x.setRenderOptions(o));
      assert(y.setRenderOptions(o));
      for (uint32_t size : {1600u, 2400u, 3200u}) {
        for (uint32_t cp = 1; cp < 65536; ++cp) {
          auto gx = x.glyphId(cp), gy = y.glyphId(cp);
          assert(bool(gx) == bool(gy));
          if (!gx) continue;
          FtFont::GlyphMetrics mx, my;
          assert(x.metricsGlyph26_6(gx, size, mx));
          assert(y.metricsGlyph26_6(gy, size, my));
          assert(mx.advance26_6 == my.advance26_6 && mx.left == my.left && mx.top == my.top && mx.width == my.width &&
                 mx.height == my.height);
          auto px = x.rasterizeGlyph26_6(gx, size), py = y.rasterizeGlyph26_6(gy, size);
          assert(px && py);
          assert(px->width == py->width && px->height == py->height && px->xoff == py->xoff && px->yoff == py->yoff &&
                 px->advance == py->advance);
          if (px->width && px->height) assert(memcmp(px->pixels, py->pixels, size_t(px->width) * px->height) == 0);
          ++count;
        }
        for (uint32_t l = 32; l < 127; ++l)
          for (uint32_t r = 32; r < 127; ++r)
            assert(x.kerningGlyphs26_6(x.glyphId(l), x.glyphId(r), size) ==
                   y.kerningGlyphs26_6(y.glyphId(l), y.glyphId(r), size));
        for (const auto& seq :
             std::vector<std::vector<uint32_t>>{{'f', 'f'}, {'f', 'i'}, {'f', 'l'}, {'f', 'f', 'i'}, {'f', 'f', 'l'}}) {
          auto gx = x.ligatureGlyphId(seq.data(), seq.size()), gy = y.ligatureGlyphId(seq.data(), seq.size());
          assert(bool(gx) == bool(gy));
          if (!gx) continue;
          auto px = x.rasterizeGlyph26_6(gx, size), py = y.rasterizeGlyph26_6(gy, size);
          assert(px && py);
          assert(px->width == py->width && px->height == py->height);
          if (px->width && px->height) assert(memcmp(px->pixels, py->pixels, size_t(px->width) * px->height) == 0);
        }
      }
    }
    printf("%s equivalent\n", name);
  }
  printf("%zu raster comparisons passed\n", count);
}
