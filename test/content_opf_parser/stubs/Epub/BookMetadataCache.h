#pragma once

#include <string>
#include <vector>

class BookMetadataCache {
 public:
  std::vector<std::string> spine;
  void createSpineEntry(const std::string& href) { spine.push_back(href); }
};
