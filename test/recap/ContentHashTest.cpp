#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "lib/Recap/ContentHash.h"

static bool check(const char* text, const char* expected) {
  for (size_t chunk : {size_t(1), size_t(7), size_t(64), size_t(4096)}) {
    Recap::ContentHash hash;
    const size_t length = std::strlen(text);
    for (size_t p = 0; p < length;) {
      const size_t n = length - p < chunk ? length - p : chunk;
      hash.add(reinterpret_cast<const uint8_t*>(text + p), n);
      p += n;
    }
    char hex[65];
    hash.finish(hex);
    if (std::strcmp(hex, expected)) {
      std::fprintf(stderr, "SHA-256 mismatch: %s\n", hex);
      return false;
    }
  }
  return true;
}
int main() {
  if (!check("", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")) return 1;
  if (!check("abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")) return 1;
  if (!check("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
             "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"))
    return 1;
  Recap::ContentHash hash;
  const uint8_t a = 'a';
  for (size_t i = 0; i < 1000000; ++i) hash.add(&a, 1);
  char hex[65];
  hash.finish(hex);
  if (std::strcmp(hex, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0")) return 1;
  std::puts("Streaming SHA-256 vectors passed");
}
