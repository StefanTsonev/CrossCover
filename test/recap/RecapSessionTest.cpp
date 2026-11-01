#include <cstdio>
#include <cstring>

#include "lib/Recap/RecapSession.h"

struct Input {
  const std::string& bytes;
  size_t at = 0;
  static int read(void* p, uint8_t* data, size_t size) {
    auto& input = *static_cast<Input*>(p);
    const size_t n = std::min({size, input.bytes.size() - input.at, size_t(211)});
    if (n) std::memcpy(data, input.bytes.data() + input.at, n);
    input.at += n;
    return static_cast<int>(n);
  }
};
int main() {
  using Client = freeink::SecureClient;
  const std::string url = "https://recap.test", ca = "verified-root-fixture", device = "reader1";
  const std::string summary(8000, 's');
  const auto reply = [](const std::string& body) {
    return "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
  };
  Client::replies = {reply("{}"), reply("{}"), reply(summary)};
  Recap::Session session(url, ca, device);
  if (!session.init()) return 1;
  std::string bytes(40000, '\0');
  for (size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<char>(i);
  const std::string metadata = "{\"entries\":[]}", prompt = "{\"protocol\":3}";
  std::string response;
  for (unsigned i = 0; i < 3; ++i) {
    const auto& data = i == 1 ? bytes : i == 0 ? metadata : prompt;
    Input input{data};
    const int status = session.request(i == 1 ? "PUT" : "POST",
                                       i == 1   ? "/v3/book/test/stream/0/40000"
                                       : i == 0 ? "/v3/plan"
                                                : "/v3/recaps",
                                       {&input, Input::read}, data.size(), response, i == 1, i == 2 ? 8192 : 512);
    if (status != 200 || (i == 2 && response != summary)) return 1;
    const auto& wire = Client::requests.back();
    if (wire.substr(wire.find("\r\n\r\n") + 4) != data || wire.find("X-Recap-Device: reader1\r\n") == std::string::npos)
      return 1;
  }
  if (Client::connections != 1 || Client::certificate != ca) return 1;
  std::puts("PASS: embedded session composes plan, binary upload and 8 KiB response on one verified connection");
}
