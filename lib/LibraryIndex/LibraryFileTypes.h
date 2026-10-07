#pragma once

#include <FsHelpers.h>

#include <string>

namespace library {
enum FileType : uint8_t { FileEpub = 1, FileXtc = 2, FileTxt = 4, FileMarkdown = 8 };

inline uint8_t fileTypeFor(const std::string& name) {
  if (FsHelpers::checkFileExtension(name, ".epub")) return FileEpub;
  if (FsHelpers::checkFileExtension(name, ".xtc") || FsHelpers::checkFileExtension(name, ".xtch")) return FileXtc;
  if (FsHelpers::checkFileExtension(name, ".txt")) return FileTxt;
  if (FsHelpers::checkFileExtension(name, ".md")) return FileMarkdown;
  return 0;
}
}  // namespace library
