#include "RecapSession.h"

#include <Arduino.h>
#include <Logging.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#ifdef SIMULATOR
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#else
#include "../hal/HalHttpUpload.h"
#endif

bool Recap::Session::init() {
  buffer = makeUniqueNoThrow<uint8_t[]>(BUFFER_SIZE);
  if (!buffer) {
    LOG_ERR("RECAP", "OOM allocating %u-byte transfer scratch", unsigned(BUFFER_SIZE));
    return false;
  }
  return true;
}

int Recap::Session::request(const char* method, const std::string& path, RequestSource source, size_t length,
                            std::string& response, bool binary, size_t maximumResponse) {
  if (!buffer || !source.read) return -1;
  const uint32_t started = millis();
  response.clear();
  int status = -1;
#ifdef SIMULATOR
  // The desktop shim accepts text bodies only. Encoding is host-only; real
  // hardware streams binary directly from SD and never builds this body.
  std::string body;
  body.reserve(binary ? (length + 2) / 3 * 4 : length);
  static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t done = 0, carried = 0;
  uint8_t carry[3];
  while (done < length) {
    const int n = source.read(source.context, buffer.get(), std::min(BUFFER_SIZE, length - done));
    if (n <= 0 || size_t(n) > length - done) return -1;
    if (!binary)
      body.append(reinterpret_cast<const char*>(buffer.get()), n);
    else
      for (int i = 0; i < n; ++i) {
        carry[carried++] = buffer[i];
        if (carried == 3) {
          const uint32_t value = uint32_t(carry[0]) << 16 | uint32_t(carry[1]) << 8 | carry[2];
          body += alphabet[value >> 18];
          body += alphabet[value >> 12 & 63];
          body += alphabet[value >> 6 & 63];
          body += alphabet[value & 63];
          carried = 0;
        }
      }
    done += n;
    if (source.progress) source.progress(source.context, done);
  }
  if (binary && carried) {
    const uint32_t value = uint32_t(carry[0]) << 16 | (carried == 2 ? uint32_t(carry[1]) << 8 : 0);
    body += alphabet[value >> 18];
    body += alphabet[value >> 12 & 63];
    body += carried == 2 ? alphabet[value >> 6 & 63] : '=';
    body += '=';
  }
  HTTPClient http;
  WiFiClientSecure transport;
  http.setTimeout(60000);
  http.begin(transport, (url + path).c_str());
  http.addHeader("User-Agent", "CrossCover-Recap");
  http.addHeader("X-Recap-Device", device.c_str());
  http.addHeader("Content-Type", binary ? "application/octet-stream" : "application/json");
  if (binary) http.addHeader("X-Recap-Encoding", "base64");
  status = std::strcmp(method, "PUT") == 0 ? http.PUT(body.c_str()) : http.POST(body.c_str());
  const auto text = http.getString();
  if (text.length() > maximumResponse)
    status = -1;
  else
    response.assign(text.c_str(), text.length());
  http.end();
#else
  const auto authority = url.substr(8);
  if (url.rfind("https://", 0) || authority.empty() || authority.find_first_of("/[]@?#\r\n") != std::string::npos)
    return -1;
  const auto colon = authority.find(':');
  const auto host = authority.substr(0, colon);
  uint16_t port = 443;
  if (colon != std::string::npos) {
    char* end = nullptr;
    const long parsed = std::strtol(authority.c_str() + colon + 1, &end, 10);
    if (*end || parsed <= 0 || parsed > 65535) return -1;
    port = static_cast<uint16_t>(parsed);
  }
  if (!client.connected()) {
    if (ESP.getFreeHeap() < 55000 || ESP.getMaxAllocHeap() < 24000) {
      LOG_ERR("RECAP", "Insufficient internal heap for TLS: free=%u max=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
      return -1;
    }
    client.setCACert(ca.c_str());
    client.setTimeout(60000);
    if (!client.connect(host.c_str(), port)) {
      LOG_ERR("RECAP", "TLS connection failed");
      return -1;
    }
    LOG_INF("RECAP", "TLS session ready in %lu ms", static_cast<unsigned long>(millis() - started));
  }
  std::string headers;
  headers.reserve(512);
  headers = std::string(method) + " " + path + " HTTP/1.1\r\nHost: " + authority +
            "\r\nUser-Agent: CrossCover-Recap\r\nX-Recap-Device: " + device +
            "\r\nContent-Type: " + (binary ? "application/octet-stream" : "application/json") +
            "\r\nAccept-Encoding: identity\r\nConnection: keep-alive\r\nContent-Length: " + std::to_string(length) +
            "\r\n\r\n";
  const HalHttpUpload::Transport socket{
      &client,
      [](void* p, const uint8_t* data, size_t size) {
        return static_cast<freeink::SecureClient*>(p)->write(data, size);
      },
      [](void* p, uint8_t* data, size_t size) { return static_cast<freeink::SecureClient*>(p)->read(data, size); },
      [](void* p) { return bool(static_cast<freeink::SecureClient*>(p)->connected()); }};
  const HalHttpUpload::Source input{source.context, source.read, source.progress};
  const HalHttpUpload::Clock clock{[]() { return uint32_t(millis()); }, []() { delay(1); }};
  const auto result = HalHttpUpload::put(socket, input, clock, headers, length, buffer.get(), BUFFER_SIZE, response,
                                         60000, maximumResponse);
  LOG_INF("RECAP", "Transfer body=%lu ms average=%llu B/s free=%u maxAlloc=%u",
          static_cast<unsigned long>(result.bodyMs),
          static_cast<unsigned long long>(result.bodyMs ? uint64_t(result.sent) * 1000 / result.bodyMs : 0),
          ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  status = result.complete ? result.status : -1;
  if (!result.complete || !result.reusable) client.stop();
#endif
  LOG_INF("RECAP", "%s %s bytes=%u http=%d elapsed=%lu ms", method, path.c_str(), unsigned(length), status,
          static_cast<unsigned long>(millis() - started));
  return status;
}
