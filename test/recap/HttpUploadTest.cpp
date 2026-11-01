#include <cstdio>
#include <cstring>
#include <vector>

#include "lib/hal/HalHttpUpload.h"

namespace {
uint32_t ticks = 0;
uint32_t now() { return ticks; }
void idle() { ++ticks; }
struct Fixture {
  std::vector<uint8_t> file;
  std::string wire, reply;
  size_t sourceAt = 0, replyAt = 0, progress = 0;
  size_t maxWrite = 7, failAt = SIZE_MAX;
  bool stall = false, readBeforeBody = false;
  size_t expectedWire = 0;
  static size_t write(void* p, const uint8_t* data, size_t size) {
    auto& f = *static_cast<Fixture*>(p);
    if (f.stall) return 0;
    const auto count = std::min(f.maxWrite, size);
    f.wire.append(reinterpret_cast<const char*>(data), count);
    return count;
  }
  static int read(void* p, uint8_t* data, size_t size) {
    auto& f = *static_cast<Fixture*>(p);
    if (f.wire.size() != f.expectedWire) f.readBeforeBody = true;
    if (f.stall) return -1;
    const auto count = std::min(size, f.reply.size() - f.replyAt);
    memcpy(data, f.reply.data() + f.replyAt, count);
    f.replyAt += count;
    return count;
  }
  static bool connected(void* p) {
    const auto& f = *static_cast<Fixture*>(p);
    return f.stall || f.replyAt < f.reply.size();
  }
  static int source(void* p, uint8_t* data, size_t size) {
    auto& f = *static_cast<Fixture*>(p);
    if (f.sourceAt >= f.failAt) return -1;
    const auto count = std::min({size, f.file.size() - f.sourceAt, size_t(17)});
    memcpy(data, f.file.data() + f.sourceAt, count);
    f.sourceAt += count;
    return count;
  }
  static void report(void* p, size_t count) { static_cast<Fixture*>(p)->progress = count; }
  HalHttpUpload::Result run(std::string& body) {
    const std::string headers = "PUT /stream/0 HTTP/1.1\r\nContent-Length: " + std::to_string(file.size()) + "\r\n\r\n";
    expectedWire = wire.size() + headers.size() + file.size();
    uint8_t buffer[128];
    return HalHttpUpload::put({this, write, read, connected}, {this, source, report}, {now, idle}, headers, file.size(),
                              buffer, sizeof(buffer), body, 100);
  }
};
bool check(bool value, const char* message) {
  if (!value) std::fprintf(stderr, "FAIL: %s\n", message);
  return value;
}
}  // namespace

int main() {
  bool ok = true;
  Fixture f;
  f.file.resize(100000);
  for (size_t i = 0; i < f.file.size(); ++i) f.file[i] = static_cast<uint8_t>(i);
  f.reply = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n{}";
  std::string body;
  auto result = f.run(body);
  ok &= check(result.complete && result.status == 200 && result.sent == f.file.size() && body == "{}",
              "binary upload and bounded response");
  ok &= check(!f.readBeforeBody && f.progress == f.file.size(), "no response wait between body blocks");
  const auto begin = f.wire.find("\r\n\r\n") + 4;
  ok &= check(f.wire.size() == begin + f.file.size() && !memcmp(f.wire.data() + begin, f.file.data(), f.file.size()),
              "partial socket writes preserve every byte including NUL");
  ok &= check(f.wire.find("PUT", 1) == std::string::npos, "one HTTP request for the entire source");
  Fixture reused;
  reused.file = {1, 2, 3};
  reused.reply = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n{}HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n{}";
  result = reused.run(body);
  ok &= check(result.complete && result.reusable, "framed keep-alive response can reuse TLS");
  reused.sourceAt = 0;
  result = reused.run(body);
  ok &=
      check(result.complete && result.reusable && !reused.readBeforeBody, "second segment uses clean response framing");

  for (const auto* response :
       {"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1\r\n{\r\n1\r\n}\r\n0\r\nX-Trailer: value\r\n\r\n",
        "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n{}"}) {
    Fixture framing;
    framing.file = {1, 2, 3};
    framing.reply = response;
    result = framing.run(body);
    ok &= check(result.complete && body == "{}", "chunked and close-delimited responses");
    if (std::string(response).find("Connection: close") != std::string::npos)
      ok &= check(!result.reusable, "close-delimited response must reconnect");
  }
  for (const auto* response :
       {"HTTP/1.1 200 OK\r\nContent-Length: 513\r\n\r\n{}", "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\n{}",
        "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nTransfer-Encoding: chunked\r\n\r\n{}",
        "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1\r\n{\r\n",
        "HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\n\r\n{}", "not HTTP\r\n\r\n{}"}) {
    Fixture invalid;
    invalid.file = {1};
    invalid.reply = response;
    ok &= check(!invalid.run(body).complete, "reject oversized, truncated and ambiguous responses");
  }
  Fixture failedSource;
  failedSource.file.resize(100);
  failedSource.failAt = 17;
  ok &= check(!failedSource.run(body).complete && failedSource.sourceAt == 17, "source read failure stops upload");
  Fixture stalled;
  stalled.stall = true;
  stalled.file = {1};
  const auto started = ticks;
  ok &= check(!stalled.run(body).complete && ticks - started <= 101, "stalled transport times out");
  if (ok) std::puts("PASS: streaming HTTP sender, partial I/O, framing, limits and timeouts");
  return ok ? 0 : 1;
}
