#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

// Streaming SHA-256 shared by embedded and native builds. Fixed 112-byte state;
// the transform uses a circular 16-word schedule instead of a 256-byte array.
namespace Recap {
class ContentHash {
 public:
  void add(const uint8_t* data, size_t length) {
    bytes += length;
    while (length) {
      const size_t n = length < 64 - used ? length : 64 - used;
      std::memcpy(block + used, data, n);
      used += n;
      data += n;
      length -= n;
      if (used == 64) {
        transform();
        used = 0;
      }
    }
  }
  void finish(char hex[65]) {
    const uint64_t bits = bytes * 8;
    block[used++] = 0x80;
    if (used > 56) {
      std::memset(block + used, 0, 64 - used);
      transform();
      used = 0;
    }
    std::memset(block + used, 0, 56 - used);
    for (int i = 0; i < 8; ++i) block[63 - i] = static_cast<uint8_t>(bits >> (i * 8));
    transform();
    static constexpr char digits[] = "0123456789abcdef";
    for (int i = 0; i < 32; ++i) {
      const uint8_t b = static_cast<uint8_t>(state[i / 4] >> (24 - i % 4 * 8));
      hex[i * 2] = digits[b >> 4];
      hex[i * 2 + 1] = digits[b & 15];
    }
    hex[64] = '\0';
  }

 private:
  static uint32_t rotate(uint32_t x, unsigned n) { return x >> n | x << (32 - n); }
  void transform() {
    static constexpr uint32_t k[] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    uint32_t w[16];
    for (int i = 0; i < 16; ++i)
      w[i] = uint32_t(block[i * 4]) << 24 | uint32_t(block[i * 4 + 1]) << 16 | uint32_t(block[i * 4 + 2]) << 8 |
             block[i * 4 + 3];
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4], f = state[5], g = state[6],
             h = state[7];
    for (unsigned i = 0; i < 64; ++i) {
      if (i >= 16) {
        const uint32_t x = w[(i - 15) & 15], y = w[(i - 2) & 15];
        w[i & 15] +=
            (rotate(x, 7) ^ rotate(x, 18) ^ (x >> 3)) + w[(i - 7) & 15] + (rotate(y, 17) ^ rotate(y, 19) ^ (y >> 10));
      }
      const uint32_t t1 = h + (rotate(e, 6) ^ rotate(e, 11) ^ rotate(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i & 15];
      const uint32_t t2 = (rotate(a, 2) ^ rotate(a, 13) ^ rotate(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
      h = g;
      g = f;
      f = e;
      e = d + t1;
      d = c;
      c = b;
      b = a;
      a = t1 + t2;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
  }
  uint32_t state[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  uint8_t block[64]{};
  uint64_t bytes = 0;
  size_t used = 0;
};
}  // namespace Recap
