#pragma once

#include <cstdlib>
#include <memory>
#include <type_traits>
#include <utility>

#include "HalStorage.h"

template <typename T, typename... Args>
  requires(!std::is_array_v<T>)
std::unique_ptr<T> makeUniqueNoThrow(Args&&... args) {
  if (fake::fail(fake::failAlloc)) return nullptr;
  return std::make_unique<T>(std::forward<Args>(args)...);
}

template <typename T>
  requires std::is_unbounded_array_v<T>
std::unique_ptr<T> makeUniqueNoThrow(size_t count) {
  if (fake::fail(fake::failAlloc)) return nullptr;
  return std::make_unique<T>(count);
}

namespace fake {
// Models a device with PSRAM: libraryBookLimit() rises and builder arrays
// request the PSRAM pool first.
inline bool psram = false;
inline unsigned psramAllocations = 0;
}  // namespace fake

struct HeapByteBufferDeleter {
  void operator()(uint8_t* ptr) const { std::free(ptr); }
};
using HeapByteBuffer = std::unique_ptr<uint8_t[], HeapByteBufferDeleter>;

enum class MemoryPool : uint8_t { None, Internal, Psram };

inline bool psramHeapAvailable() { return fake::psram; }

inline HeapByteBuffer makeAlignedByteBufferNoThrow(const size_t count, const MemoryPool pool = MemoryPool::None) {
  if (count == 0 || fake::fail(fake::failAlloc)) return {};
  if (pool == MemoryPool::Psram) {
    if (!fake::psram) return {};
    fake::psramAllocations++;
  }
  return HeapByteBuffer(static_cast<uint8_t*>(std::malloc(count)));
}

template <typename F>
struct [[nodiscard]] ScopedCleanup final {
  const F fn;
  explicit ScopedCleanup(F f) : fn{std::move(f)} {}
  ScopedCleanup(const ScopedCleanup&) = delete;
  ScopedCleanup& operator=(const ScopedCleanup&) = delete;
  ~ScopedCleanup() { fn(); }
};

template <typename F>
ScopedCleanup(F) -> ScopedCleanup<F>;
