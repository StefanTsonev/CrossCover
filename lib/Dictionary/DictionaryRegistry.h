#pragma once

#include <string>
#include <vector>

struct DictionaryEntry {
  std::string name;
  std::string stem;
};

namespace DictionaryRegistry {
void discover(std::vector<DictionaryEntry>& out);
bool resolveBasePath(const char* folderName, std::string& basePathOut);
}  // namespace DictionaryRegistry
