#pragma once

#include <string>

#include "HalStorage.h"

class Xtc {
  std::string path;

 public:
  Xtc(const std::string& path, const char*) : path(path) {}
  std::string getCachePath() const { return "/.crosspoint/cache_" + path.substr(1); }
  bool clearCache() const {
    const std::string cachePath = getCachePath();
    return !Storage.exists(cachePath.c_str()) || Storage.removeDir(cachePath.c_str());
  }
};
