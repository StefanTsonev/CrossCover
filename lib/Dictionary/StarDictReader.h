#pragma once

#include <HalStorage.h>

#include <cstdint>
#include <string>
#include <utility>

class StarDictReader {
 public:
  enum class Result { Found, NotFound, Missing, Invalid, Error };

  explicit StarDictReader(std::string basePath = "/.crosspoint/dictionaries/en-bg/en-bg")
      : basePath(std::move(basePath)) {}

  Result lookup(const std::string& word, std::string& definition);
  bool needsIndex() const;
  bool buildIndex();

  static constexpr const char* DICTIONARY_BASE = "/.crosspoint/dictionaries/en-bg/en-bg";

 private:
  std::string basePath;
  static constexpr uint32_t QIDX_MAGIC = 0x58444951;
  static constexpr uint32_t QIDX_VERSION = 1;
  static constexpr uint32_t QIDX_INTERVAL = 256;
  static bool readBigEndian32(FsFile& file, uint32_t& value);
  static bool readIndexWord(FsFile& file, std::string& word);
  static std::string normalizedWord(const std::string& word);
};
