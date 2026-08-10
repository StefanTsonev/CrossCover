#include "DictionaryRegistry.h"

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cctype>
#include <cstring>

namespace {
constexpr char DICTIONARY_ROOT[] = "/.crosspoint/dictionaries";

bool findStem(const std::string& folder, std::string& stem) {
  auto dir = Storage.open(folder.c_str());
  if (!dir || !dir.isDirectory()) return false;
  dir.rewindDirectory();
  char name[128]{};
  std::string found;
  for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    entry.getName(name, sizeof(name));
    const size_t length = std::strlen(name);
    if (entry.isDirectory() || length <= 4 || std::strcmp(name + length - 4, ".idx") != 0) continue;
    name[length - 4] = '\0';
    if (!found.empty() && found != name) return false;
    found = name;
  }
  if (found.empty()) return false;
  const std::string base = folder + "/" + found;
  if (!Storage.exists((base + ".dict").c_str())) return false;
  stem = std::move(found);
  return true;
}
}  // namespace

namespace DictionaryRegistry {
void discover(std::vector<DictionaryEntry>& out) {
  out.clear();
  auto root = Storage.open(DICTIONARY_ROOT);
  if (!root || !root.isDirectory()) return;
  root.rewindDirectory();
  char name[128]{};
  for (auto entry = root.openNextFile(); entry; entry = root.openNextFile()) {
    entry.getName(name, sizeof(name));
    if (!entry.isDirectory() || name[0] == '.') continue;
    std::string stem;
    const std::string folder = std::string(DICTIONARY_ROOT) + "/" + name;
    if (findStem(folder, stem)) out.push_back({name, std::move(stem)});
  }
  std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
    return std::lexicographical_compare(a.name.begin(), a.name.end(), b.name.begin(), b.name.end(), [](char x, char y) {
      return std::tolower(static_cast<unsigned char>(x)) < std::tolower(static_cast<unsigned char>(y));
    });
  });
}

bool resolveBasePath(const char* folderName, std::string& basePathOut) {
  if (!folderName || !folderName[0] || folderName[0] == '.' || std::strpbrk(folderName, "/\\")) return false;
  std::string stem;
  const std::string folder = std::string(DICTIONARY_ROOT) + "/" + folderName;
  if (!findStem(folder, stem)) return false;
  basePathOut = folder + "/" + stem;
  return true;
}
}  // namespace DictionaryRegistry
