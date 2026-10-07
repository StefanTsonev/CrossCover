#pragma once

#include <string>

#include "Epub.h"

namespace serialization {

template <typename T>
void writePod(HalFile& file, const T& value) {
  file.write(&value, sizeof(value));
}

template <typename T>
bool tryReadPod(HalFile& file, T& value) {
  return file.read(&value, sizeof(value)) == sizeof(value);
}

inline void writeString(HalFile& file, const std::string& value) {
  writePod(file, static_cast<uint32_t>(value.size()));
  file.write(value.data(), value.size());
}
inline bool tryReadString(HalFile& file, std::string& out) {
  uint32_t length = 0;
  if (!tryReadPod(file, length) || length > static_cast<uint32_t>(file.available())) return false;
  out.resize(length);
  return file.read(out.data(), length) == static_cast<int>(length);
}

}  // namespace serialization
