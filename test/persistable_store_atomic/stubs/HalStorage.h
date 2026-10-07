#pragma once

#include <Arduino.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <unordered_map>

class HalFile {
 public:
  explicit HalFile(String content = {}) : content(std::move(content)) {}
  int read(void* output, size_t length) {
    const size_t count = std::min(length, content.size() - position);
    memcpy(output, content.data() + position, count);
    position += count;
    return static_cast<int>(count);
  }
  size_t write(const uint8_t*, size_t length) { return length; }
  void close() {
    content.clear();
    position = 0;
  }

 private:
  String content;
  size_t position = 0;
};

using FsFile = HalFile;

class HalStorage {
 public:
  void reset() {
    files.clear();
    failRenameFrom.clear();
    writeAttempts = 0;
    failWrite = false;
  }

  bool mkdir(const char*, bool = true) { return true; }
  bool exists(const char* path) const { return files.contains(path); }
  bool remove(const char* path) { return files.erase(path) > 0; }

  bool rename(const char* from, const char* to) {
    if (failRenameFrom == from) {
      failRenameFrom.clear();
      return false;
    }
    const auto source = files.find(from);
    if (source == files.end() || files.contains(to)) return false;
    files[to] = source->second;
    files.erase(source);
    return true;
  }

  bool writeFile(const char* path, const String& content) {
    ++writeAttempts;
    if (failWrite) {
      failWrite = false;
      files[path] = "";  // Simulate a failed write after truncation.
      return false;
    }
    files[path] = content.c_str();
    return true;
  }

  String readFile(const char* path) const {
    const auto file = files.find(path);
    return file == files.end() ? String() : String(file->second.c_str());
  }

  void put(const std::string& path, std::string content) { files[path] = std::move(content); }
  void failNextRenameFrom(std::string path) { failRenameFrom = std::move(path); }
  void failNextWrite() { failWrite = true; }
  bool openFileForRead(const char*, const char* path, HalFile& file) const {
    if (!exists(path)) return false;
    file = HalFile(readFile(path));
    return true;
  }
  size_t writeAttempts = 0;

 private:
  std::unordered_map<std::string, std::string> files;
  std::string failRenameFrom;
  bool failWrite = false;
};

inline HalStorage Storage;
