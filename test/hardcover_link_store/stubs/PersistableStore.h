#pragma once
#include <ArduinoJson.h>

#include <string>

namespace HardcoverTestStorage {
inline std::string file;
inline bool failWrite = false;
inline int writes = 0;
inline void reset() {
  file.clear();
  failWrite = false;
  writes = 0;
}
}  // namespace HardcoverTestStorage

class PersistableStoreBase {
 public:
  static bool readDocFromFile(const char*, JsonDocument& doc) {
    return !deserializeJson(doc, HardcoverTestStorage::file);
  }
  static bool writeDocToFileAtomically(const char*, JsonDocument& doc) {
    if (HardcoverTestStorage::failWrite) return false;
    std::string replacement;
    serializeJson(doc, replacement);
    HardcoverTestStorage::file = std::move(replacement);
    ++HardcoverTestStorage::writes;
    return true;
  }
};
