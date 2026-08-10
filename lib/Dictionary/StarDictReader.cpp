#include "StarDictReader.h"

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <vector>

namespace {
constexpr size_t MAX_INDEX_WORD_BYTES = 256;
constexpr size_t MAX_DEFINITION_BYTES = 8192;

bool isTrimCharacter(const unsigned char c) { return std::isspace(c) || std::ispunct(c); }

bool normalizedEquals(const std::string& indexed, const std::string& wanted) {
  size_t start = 0;
  size_t end = indexed.size();
  while (start < end && isTrimCharacter(static_cast<unsigned char>(indexed[start]))) ++start;
  while (end > start && isTrimCharacter(static_cast<unsigned char>(indexed[end - 1]))) --end;
  if (end - start != wanted.size()) return false;
  for (size_t i = 0; i < wanted.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(indexed[start + i]);
    const char lower = static_cast<char>(c < 128 ? std::tolower(c) : c);
    if (lower != wanted[i]) return false;
  }
  return true;
}
}  // namespace

bool StarDictReader::readBigEndian32(FsFile& file, uint32_t& value) {
  uint8_t bytes[4]{};
  if (file.read(bytes, sizeof(bytes)) != sizeof(bytes)) return false;
  value = (static_cast<uint32_t>(bytes[0]) << 24) | (static_cast<uint32_t>(bytes[1]) << 16) |
          (static_cast<uint32_t>(bytes[2]) << 8) | static_cast<uint32_t>(bytes[3]);
  return true;
}

bool StarDictReader::readIndexWord(FsFile& file, std::string& word) {
  word.clear();
  for (size_t i = 0; i < MAX_INDEX_WORD_BYTES; ++i) {
    const int value = file.read();
    if (value < 0) return false;
    if (value == 0) return true;
    word.push_back(static_cast<char>(value));
  }
  return false;
}

std::string StarDictReader::normalizedWord(const std::string& word) {
  size_t start = 0;
  size_t end = word.size();
  while (start < end && isTrimCharacter(static_cast<unsigned char>(word[start]))) ++start;
  while (end > start && isTrimCharacter(static_cast<unsigned char>(word[end - 1]))) --end;

  std::string normalized;
  normalized.reserve(end - start);
  for (size_t i = start; i < end; ++i) {
    const unsigned char c = static_cast<unsigned char>(word[i]);
    normalized.push_back(static_cast<char>(c < 128 ? std::tolower(c) : c));
  }
  return normalized;
}

bool StarDictReader::needsIndex() const {
  FsFile index;
  if (!Storage.openFileForRead("DICT", basePath + ".idx", index)) return false;
  const uint32_t indexSize = static_cast<uint32_t>(index.fileSize());
  FsFile sidecar;
  if (!Storage.openFileForRead("DICT", basePath + ".qidx", sidecar)) return true;
  uint32_t header[4]{};
  return sidecar.read(header, sizeof(header)) != sizeof(header) || header[0] != QIDX_MAGIC ||
         header[1] != QIDX_VERSION || header[2] != indexSize;
}

bool StarDictReader::buildIndex() {
  FsFile index;
  if (!Storage.openFileForRead("DICT", basePath + ".idx", index)) return false;
  const uint32_t indexSize = static_cast<uint32_t>(index.fileSize());
  FsFile sidecar;
  const std::string sidecarPath = basePath + ".qidx";
  if (!Storage.openFileForWrite("DICT", sidecarPath, sidecar)) return false;
  const uint32_t placeholder[4] = {0, 0, 0, 0};
  if (sidecar.write(placeholder, sizeof(placeholder)) != sizeof(placeholder)) return false;
  uint32_t entry = 0;
  uint32_t offset = 0;
  uint32_t samples = 0;
  if (sidecar.write(&offset, sizeof(offset)) != sizeof(offset)) return false;
  samples = 1;
  while (offset < indexSize) {
    const int value = index.read();
    if (value < 0) return false;
    ++offset;
    if (value != 0) continue;
    uint8_t suffix[8];
    if (index.read(suffix, sizeof(suffix)) != sizeof(suffix)) return false;
    offset += sizeof(suffix);
    ++entry;
    if (entry % QIDX_INTERVAL == 0 && offset < indexSize) {
      if (sidecar.write(&offset, sizeof(offset)) != sizeof(offset)) return false;
      ++samples;
    }
  }
  const uint32_t header[4] = {QIDX_MAGIC, QIDX_VERSION, indexSize, samples};
  if (!sidecar.seek(0) || sidecar.write(header, sizeof(header)) != sizeof(header)) return false;
  sidecar.close();
  return true;
}

StarDictReader::Result StarDictReader::lookup(const std::string& input, std::string& definition) {
  definition.clear();
  const std::string wanted = normalizedWord(input);
  if (wanted.empty()) return Result::NotFound;

  const std::string ifoPath = basePath + ".ifo";
  const std::string idxPath = basePath + ".idx";
  const std::string dictPath = basePath + ".dict";

  FsFile ifo;
  if (!Storage.openFileForRead("DICT", ifoPath, ifo)) return Result::Missing;
  ifo.close();

  if (needsIndex() && !buildIndex()) LOG_ERR("DICT", "Could not build dictionary index");

  FsFile index;
  if (!Storage.openFileForRead("DICT", idxPath, index)) return Result::Missing;

  uint32_t offset = 0;
  uint32_t length = 0;
  std::string indexedWord;
  bool found = false;
  uint32_t scanStart = 0;
  FsFile sidecar;
  if (Storage.openFileForRead("DICT", basePath + ".qidx", sidecar)) {
    uint32_t header[4]{};
    if (sidecar.read(header, sizeof(header)) == sizeof(header) && header[0] == QIDX_MAGIC &&
        header[1] == QIDX_VERSION && header[2] == index.fileSize() && header[3] > 0) {
      uint32_t low = 0;
      uint32_t high = header[3] - 1;
      while (low < high) {
        const uint32_t middle = (low + high + 1) / 2;
        uint32_t candidateOffset = 0;
        if (!sidecar.seek(sizeof(header) + middle * sizeof(candidateOffset)) ||
            sidecar.read(&candidateOffset, sizeof(candidateOffset)) != sizeof(candidateOffset) ||
            !index.seek(candidateOffset)) {
          break;
        }
        if (!readIndexWord(index, indexedWord)) break;
        if (normalizedWord(indexedWord) <= wanted)
          low = middle;
        else
          high = middle - 1;
      }
      sidecar.seek(sizeof(header) + low * sizeof(scanStart));
      sidecar.read(&scanStart, sizeof(scanStart));
    }
  }
  index.seek(scanStart);
  while (index.available() > 0) {
    if (!readIndexWord(index, indexedWord) || !readBigEndian32(index, offset) || !readBigEndian32(index, length)) {
      index.close();
      LOG_ERR("DICT", "Invalid StarDict index");
      return Result::Invalid;
    }
    if (normalizedEquals(indexedWord, wanted)) {
      found = true;
      break;
    }
    if (normalizedWord(indexedWord) > wanted) break;
  }
  index.close();

  if (!found) {
    // Small, conservative stemming fallback for common English inflections.
    std::vector<std::string> variants;
    if (wanted.size() > 2 && wanted.ends_with("'s")) variants.push_back(wanted.substr(0, wanted.size() - 2));
    if (wanted.size() > 3 && wanted.ends_with("ies")) variants.push_back(wanted.substr(0, wanted.size() - 3) + "y");
    if (wanted.size() > 2 && wanted.ends_with("es")) variants.push_back(wanted.substr(0, wanted.size() - 2));
    if (wanted.size() > 1 && wanted.ends_with("s")) variants.push_back(wanted.substr(0, wanted.size() - 1));
    if (wanted.size() > 3 && wanted.ends_with("ing")) {
      const std::string stem = wanted.substr(0, wanted.size() - 3);
      variants.push_back(stem);
      if (stem.size() > 1 && stem.back() == stem[stem.size() - 2]) variants.push_back(stem.substr(0, stem.size() - 1));
    }
    if (wanted.size() > 2 && wanted.ends_with("ed")) variants.push_back(wanted.substr(0, wanted.size() - 2));
    for (const auto& variant : variants) {
      if (variant == wanted) continue;
      const Result result = lookup(variant, definition);
      if (result == Result::Found) return result;
    }
    return Result::NotFound;
  }
  if (length == 0 || length > MAX_DEFINITION_BYTES) return Result::Invalid;

  FsFile dict;
  if (!Storage.openFileForRead("DICT", dictPath, dict)) return Result::Missing;
  if (!dict.seek(offset)) {
    dict.close();
    return Result::Invalid;
  }
  definition.resize(length);
  if (dict.read(definition.data(), length) != static_cast<int>(length)) {
    dict.close();
    definition.clear();
    return Result::Error;
  }
  dict.close();

  // StarDict definitions can contain NUL separators between dictionary fields.
  std::replace(definition.begin(), definition.end(), '\0', '\n');
  return Result::Found;
}
