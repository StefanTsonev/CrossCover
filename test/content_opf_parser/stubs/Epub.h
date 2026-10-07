#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// In-memory manifest file shared across write/read opens for spine lookup tests.
class HalFile {
 public:
  explicit operator bool() const { return open_; }
  void close() { open_ = false; }
  bool seek(size_t position) {
    position_ = position;
    return true;
  }
  bool seekCur(int64_t offset) {
    position_ += static_cast<size_t>(offset);
    return true;
  }
  size_t position() const { return position_; }
  size_t size() const { return bytes_ ? bytes_->size() : 0; }
  int available() const { return static_cast<int>(size() - position_); }
  int read(void* out, size_t length) {
    if (!open_ || position_ > size()) return 0;
    const size_t count = std::min(length, size() - position_);
    if (count) std::memcpy(out, bytes_->data() + position_, count);
    position_ += count;
    return static_cast<int>(count);
  }
  size_t write(const void* data, size_t length) {
    if (!open_) return 0;
    bytes_->resize(std::max(bytes_->size(), position_ + length));
    std::memcpy(bytes_->data() + position_, data, length);
    position_ += length;
    return length;
  }
  void markOpen(std::vector<uint8_t>& bytes) {
    bytes_ = &bytes;
    position_ = 0;
    open_ = true;
  }

 private:
  std::vector<uint8_t>* bytes_ = nullptr;
  bool open_ = false;
  size_t position_ = 0;
};

struct TestStorage {
  int writeOpens = 0;
  int readOpens = 0;
  std::vector<uint8_t> bytes;

  bool openFileForWrite(const char*, const std::string&, HalFile& file) {
    writeOpens++;
    bytes.clear();
    file.markOpen(bytes);
    return true;
  }
  bool openFileForRead(const char*, const std::string&, HalFile& file) {
    readOpens++;
    file.markOpen(bytes);
    return true;
  }
  bool exists(const char*) const { return false; }
  bool remove(const char*) { return true; }
};

inline TestStorage Storage;
