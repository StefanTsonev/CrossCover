#pragma once

#include <string>

#include "HalStorage.h"

namespace FsHelpers {
inline bool checkFileExtension(const std::string& path, const char* extension) { return path.ends_with(extension); }
inline bool hasEpubExtension(const std::string& path) { return checkFileExtension(path, ".epub"); }
inline bool hasXtcExtension(const std::string& path) { return checkFileExtension(path, ".xtc"); }
inline bool hasTxtExtension(const std::string& path) { return checkFileExtension(path, ".txt"); }
inline bool directoryIterationFailed(const HalFile& directory) {
  return directory.allocationFailed() || directory.iterationFailed();
}
}  // namespace FsHelpers
