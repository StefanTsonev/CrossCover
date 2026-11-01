#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
namespace freeink {
class SecureClient {
 public:
  static inline unsigned connections = 0;
  static inline std::string certificate;
  static inline std::vector<std::string> requests;
  static inline std::vector<std::string> replies;
  void setCACert(const char* ca) { certificate = ca; }
  void setTimeout(unsigned) {}
  bool connect(const char*, uint16_t) {
    ++connections;
    open = true;
    return true;
  }
  bool connected() { return open; }
  void stop() { open = false; }
  size_t write(const uint8_t* data, size_t size) {
    const size_t n = std::min(size, size_t(29));
    wire.append(reinterpret_cast<const char*>(data), n);
    const auto split = wire.find("\r\n\r\n");
    if (split != std::string::npos) {
      const auto start = wire.find("Content-Length: ") + 16;
      const size_t length = std::stoul(wire.substr(start));
      if (wire.size() == split + 4 + length) {
        requests.push_back(wire);
        wire.clear();
        reply = replies.at(requests.size() - 1);
        at = 0;
      }
    }
    return n;
  }
  int read(uint8_t* out, size_t size) {
    const size_t n = std::min(size, reply.size() - at);
    if (n) std::memcpy(out, reply.data() + at, n);
    at += n;
    return static_cast<int>(n);
  }

 private:
  bool open = false;
  std::string wire, reply;
  size_t at = 0;
};
}  // namespace freeink
