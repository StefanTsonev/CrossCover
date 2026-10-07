#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace scalable_gsub {

struct View {
  const uint8_t* data = nullptr;
  size_t size = 0;

  bool has(size_t offset, size_t count) const { return offset <= size && count <= size - offset; }
  unsigned u16(size_t offset) const { return has(offset, 2) ? unsigned(data[offset]) * 256 + data[offset + 1] : 0; }
  uint32_t u32(size_t offset) const { return has(offset, 4) ? (uint32_t(u16(offset)) << 16) | u16(offset + 2) : 0; }
  View sub(size_t offset) const { return has(offset, 1) ? View{data + offset, size - offset} : View{}; }
};

inline View table(const uint8_t* data, size_t bytes, const char* tag) {
  View file{data, bytes};
  const unsigned count = file.u16(4);
  if (!file.has(12, size_t(count) * 16)) return {};
  for (unsigned i = 0; i < count; ++i) {
    const size_t position = 12 + i * 16;
    if (std::memcmp(data + position, tag, 4)) continue;
    const auto offset = file.u32(position + 8);
    const auto length = file.u32(position + 12);
    return file.has(offset, length) ? View{data + offset, length} : View{};
  }
  return {};
}

inline int coverage(View view, unsigned glyph) {
  const unsigned format = view.u16(0);
  const unsigned count = view.u16(2);
  if (count > 4096) return -1;
  if (format == 1 && view.has(4, size_t(count) * 2)) {
    unsigned low = 0;
    unsigned high = count;
    while (low < high) {
      const unsigned middle = (low + high) / 2;
      if (view.u16(4 + middle * 2) < glyph)
        low = middle + 1;
      else
        high = middle;
    }
    return low < count && view.u16(4 + low * 2) == glyph ? int(low) : -1;
  }
  if (format == 2 && view.has(4, size_t(count) * 6)) {
    for (unsigned i = 0; i < count; ++i) {
      const size_t position = 4 + i * 6;
      const unsigned start = view.u16(position);
      const unsigned end = view.u16(position + 2);
      if (glyph >= start && glyph <= end) return int(view.u16(position + 4) + glyph - start);
    }
  }
  return -1;
}

inline unsigned ligatureGlyph(View gsub, const unsigned* sequence, unsigned length) {
  if (!gsub.has(0, 10) || length < 2 || length > 3) return 0;
  View features = gsub.sub(gsub.u16(6));
  View lookups = gsub.sub(gsub.u16(8));
  const unsigned featureCount = features.u16(0);
  const unsigned lookupCount = lookups.u16(0);
  if (featureCount > 128 || lookupCount > 1024 || !features.has(2, size_t(featureCount) * 6) ||
      !lookups.has(2, size_t(lookupCount) * 2))
    return 0;

  unsigned work = 0;
  for (unsigned featureIndex = 0; featureIndex < featureCount; ++featureIndex) {
    const size_t record = 2 + featureIndex * 6;
    if (std::memcmp(features.data + record, "liga", 4) && std::memcmp(features.data + record, "rlig", 4)) continue;
    View feature = features.sub(features.u16(record + 4));
    const unsigned count = feature.u16(2);
    if (count > 128 || !feature.has(4, size_t(count) * 2)) continue;
    for (unsigned i = 0; i < count; ++i) {
      const unsigned index = feature.u16(4 + i * 2);
      if (index >= lookupCount) continue;
      View lookup = lookups.sub(lookups.u16(2 + index * 2));
      const unsigned type = lookup.u16(0);
      const unsigned subtableCount = lookup.u16(4);
      if ((type != 4 && type != 7) || subtableCount > 128 || !lookup.has(6, size_t(subtableCount) * 2)) continue;
      for (unsigned j = 0; j < subtableCount; ++j) {
        if (++work > 512) return 0;
        View subtable = lookup.sub(lookup.u16(6 + j * 2));
        if (type == 7) {
          if (subtable.u16(0) != 1 || subtable.u16(2) != 4) continue;
          subtable = subtable.sub(subtable.u32(4));
        }
        if (subtable.u16(0) != 1) continue;
        const int coverageIndex = coverage(subtable.sub(subtable.u16(2)), sequence[0]);
        const unsigned setCount = subtable.u16(4);
        if (coverageIndex < 0 || unsigned(coverageIndex) >= setCount || !subtable.has(6, size_t(setCount) * 2))
          continue;
        View set = subtable.sub(subtable.u16(6 + unsigned(coverageIndex) * 2));
        const unsigned ligatureCount = set.u16(0);
        if (ligatureCount > 512 || !set.has(2, size_t(ligatureCount) * 2)) continue;
        for (unsigned k = 0; k < ligatureCount; ++k) {
          View ligature = set.sub(set.u16(2 + k * 2));
          if (ligature.u16(2) != length || !ligature.has(4, (length - 1) * 2)) continue;
          bool match = true;
          for (unsigned component = 1; component < length; ++component)
            match = match && ligature.u16(4 + (component - 1) * 2) == sequence[component];
          if (match) return ligature.u16(0);
        }
      }
    }
  }
  return 0;
}

}  // namespace scalable_gsub
