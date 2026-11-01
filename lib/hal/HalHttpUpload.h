#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>

// Bounded request-body streaming, which the pinned SDK HTTP client does not
// expose. The HAL receives transport/file callbacks; it owns HTTP framing and
// the error contract, without changing the SDK or buffering the whole file.
namespace HalHttpUpload {
struct Transport {
  void* context;
  size_t (*write)(void*, const uint8_t*, size_t);
  int (*read)(void*, uint8_t*, size_t);
  bool (*connected)(void*);
};
struct Source {
  void* context;
  int (*read)(void*, uint8_t*, size_t);
  void (*progress)(void*, size_t);
};
struct Clock {
  uint32_t (*now)();
  void (*idle)();
};
struct Result {
  int status = 0;
  size_t sent = 0;
  bool complete = false;
  bool reusable = false;
  uint32_t bodyMs = 0;
};

inline Result put(Transport socket, Source source, Clock clock, const std::string& headers, size_t length,
                  uint8_t* buffer, size_t capacity, std::string& response, uint32_t timeout = 60000,
                  size_t maximumResponse = 512) {
  Result result;
  response.clear();
  if (!buffer || !capacity || maximumResponse > 8192) return result;
  response.reserve(maximumResponse);
  const auto writeAll = [&](const uint8_t* data, size_t size) {
    uint32_t deadline = clock.now() + timeout;
    while (size) {
      const size_t written = socket.write(socket.context, data, size);
      if (written > size) return false;
      if (!written) {
        if (!socket.connected(socket.context) || static_cast<int32_t>(clock.now() - deadline) >= 0) return false;
        clock.idle();
        continue;
      }
      data += written;
      size -= written;
      deadline = clock.now() + timeout;
    }
    return true;
  };
  if (!writeAll(reinterpret_cast<const uint8_t*>(headers.data()), headers.size())) return result;
  const uint32_t bodyStarted = clock.now();
  while (result.sent < length) {
    const size_t want = std::min(capacity, length - result.sent);
    const int n = source.read(source.context, buffer, want);
    if (n <= 0 || static_cast<size_t>(n) > want || !writeAll(buffer, n)) return result;
    result.sent += n;
    if (source.progress) source.progress(source.context, result.sent);
    clock.idle();
  }
  result.bodyMs = clock.now() - bodyStarted;
  const uint32_t deadline = clock.now() + timeout;
  const auto byte = [&]() -> int {
    while (static_cast<int32_t>(clock.now() - deadline) < 0) {
      uint8_t value;
      const int n = socket.read(socket.context, &value, 1);
      if (n == 1) return value;
      if (!socket.connected(socket.context)) return -1;
      clock.idle();
    }
    return -1;
  };
  std::string line;
  line.reserve(512);  // One reusable header/chunk line, never per data block.
  const auto readLine = [&]() {
    line.clear();
    for (;;) {
      const int c = byte();
      if (c < 0) return false;
      if (c == '\n') {
        if (line.empty() || line.back() != '\r') return false;
        line.pop_back();
        return true;
      }
      if (line.size() >= 512) return false;
      line += static_cast<char>(c);
    }
  };
  if (!readLine() || line.size() < 12 || (line.compare(0, 9, "HTTP/1.1 ") && line.compare(0, 9, "HTTP/1.0 ")))
    return result;
  if (line[9] < '1' || line[9] > '5' || line[10] < '0' || line[10] > '9' || line[11] < '0' || line[11] > '9' ||
      (line.size() > 12 && line[12] != ' '))
    return result;
  result.status = atoi(line.c_str() + 9);
  int contentLength = -1;
  bool chunked = false;
  bool keepAlive = line.compare(0, 9, "HTTP/1.1 ") == 0;
  size_t headerBytes = 0;
  for (;;) {
    if (!readLine() || (headerBytes += line.size() + 2) > 8192) return result;
    if (line.empty()) break;
    const auto colon = line.find(':');
    if (colon == std::string::npos) return result;
    std::transform(line.begin(), line.end(), line.begin(), [](unsigned char c) {
      return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 'a' - 'A') : static_cast<char>(c);
    });
    const char* value = line.c_str() + colon + 1;
    while (*value == ' ' || *value == '\t') ++value;
    if (line.compare(0, colon, "content-length") == 0) {
      char* end = nullptr;
      const long count = strtol(value, &end, 10);
      if (!*value || *end || count < 0 || count > static_cast<long>(maximumResponse) ||
          (contentLength >= 0 && contentLength != count))
        return result;
      contentLength = count;
    } else if (line.compare(0, colon, "transfer-encoding") == 0) {
      if (std::string(value) != "chunked") return result;
      chunked = true;
    } else if (line.compare(0, colon, "connection") == 0) {
      if (std::string(value).find("close") != std::string::npos) keepAlive = false;
    } else if (line.compare(0, colon, "content-encoding") == 0 && std::string(value) != "identity")
      return result;
  }
  if (chunked && contentLength >= 0) return result;
  const auto readBody = [&](size_t count) {
    if (count > maximumResponse - response.size()) return false;
    for (size_t i = 0; i < count; ++i) {
      const int c = byte();
      if (c < 0) return false;
      response += static_cast<char>(c);
    }
    return true;
  };
  if (chunked) {
    for (;;) {
      if (!readLine()) return result;
      const auto extension = line.find(';');
      if (extension != std::string::npos) line.resize(extension);
      char* end = nullptr;
      const unsigned long count = strtoul(line.c_str(), &end, 16);
      if (line.empty() || *end || count > maximumResponse) return result;
      if (!count) {
        do {
          if (!readLine() || (headerBytes += line.size() + 2) > 8192) return result;
        } while (!line.empty());
        break;
      }
      if (!readBody(count) || !readLine() || !line.empty()) return result;
    }
  } else if (contentLength >= 0) {
    if (!readBody(contentLength)) return result;
  } else {
    for (;;) {
      const int c = byte();
      if (c < 0) {
        if (socket.connected(socket.context) || static_cast<int32_t>(clock.now() - deadline) >= 0) return result;
        break;
      }
      if (response.size() >= maximumResponse) return result;
      response += static_cast<char>(c);
    }
  }
  result.complete = true;
  result.reusable = keepAlive && (chunked || contentLength >= 0);
  return result;
}
}  // namespace HalHttpUpload
