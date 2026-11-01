#pragma once
#include <Memory.h>

#include <cstddef>
#include <cstdint>
#include <string>
#ifndef SIMULATOR
#include <SecureClient.h>
#endif

namespace Recap {
struct RequestSource {
  void* context;
  int (*read)(void*, uint8_t*, size_t);
  void (*progress)(void*, size_t) = nullptr;
};
// One request at a time on one verified TLS session. The 4 KiB buffer is also
// reused for ZIP member hashing before the first handshake; never on the stack.
class Session {
 public:
  static constexpr size_t BUFFER_SIZE = 4096;
  Session(const std::string& url, const std::string& ca, const std::string& device)
      : url(url), ca(ca), device(device) {}
  bool init();
  uint8_t* scratch() { return buffer.get(); }
  int request(const char* method, const std::string& path, RequestSource source, size_t length, std::string& response,
              bool binary = false, size_t maximumResponse = 512);

 private:
  const std::string& url;
  const std::string& ca;
  const std::string& device;
  std::unique_ptr<uint8_t[]> buffer;
#ifndef SIMULATOR
  freeink::SecureClient client;
#endif
};
}  // namespace Recap
