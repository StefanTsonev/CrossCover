#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

// Small first-fit allocator for libraries that require malloc-style callbacks
// but must stay inside a caller-owned, fixed-size arena.
class FixedArenaAllocator {
 public:
  bool initialize(void* storage, const size_t bytes) {
    if (!storage || bytes <= sizeof(Block) || reinterpret_cast<uintptr_t>(storage) % Alignment != 0) return false;
    blocks_ = new (storage) Block{bytes - sizeof(Block), nullptr, true};
    lastFailedRequest_ = 0;
    return true;
  }

  void clearFailure() { lastFailedRequest_ = 0; }
  size_t lastFailedRequest() const { return lastFailedRequest_; }
  size_t freeBytes() const {
    size_t result = 0;
    for (const Block* block = blocks_; block; block = block->next)
      if (block->free) result += block->bytes;
    return result;
  }
  size_t largestFreeBlock() const {
    size_t result = 0;
    for (const Block* block = blocks_; block; block = block->next)
      if (block->free) result = std::max(result, block->bytes);
    return result;
  }

  static void* allocate(void* context, const size_t requested) {
    auto& self = *static_cast<FixedArenaAllocator*>(context);
    if (!self.blocks_ || !requested || requested > SIZE_MAX - Alignment) return nullptr;
    const size_t bytes = aligned(requested);
    for (Block* block = self.blocks_; block; block = block->next) {
      if (!block->free || block->bytes < bytes) continue;
      split(block, bytes);
      block->free = false;
      return block + 1;
    }
    self.lastFailedRequest_ = requested;
    return nullptr;
  }

  static void deallocate(void* context, void* ptr) {
    if (!ptr) return;
    auto& self = *static_cast<FixedArenaAllocator*>(context);
    (static_cast<Block*>(ptr) - 1)->free = true;
    for (Block* block = self.blocks_; block && block->next;) {
      if (block->free && block->next->free) {
        block->bytes += sizeof(Block) + block->next->bytes;
        block->next = block->next->next;
      } else {
        block = block->next;
      }
    }
  }

  static void* reallocate(void* context, void* ptr, const size_t oldSize, const size_t newSize) {
    if (!ptr) return allocate(context, newSize);
    if (!newSize) {
      deallocate(context, ptr);
      return nullptr;
    }

    auto* block = static_cast<Block*>(ptr) - 1;
    const size_t wanted = aligned(newSize);
    if (wanted <= block->bytes) {
      split(block, wanted);
      return ptr;
    }
    if (block->next && block->next->free && block->bytes + sizeof(Block) + block->next->bytes >= wanted) {
      Block* next = block->next;
      block->bytes += sizeof(Block) + next->bytes;
      block->next = next->next;
      split(block, wanted);
      return ptr;
    }

    void* result = allocate(context, newSize);
    if (!result) return nullptr;
    std::memcpy(result, ptr, std::min({block->bytes, oldSize, newSize}));
    deallocate(context, ptr);
    return result;
  }

 private:
  struct alignas(std::max_align_t) Block {
    size_t bytes;
    Block* next;
    bool free;
  };

  static constexpr size_t Alignment = alignof(std::max_align_t);

  static constexpr size_t aligned(const size_t bytes) { return (bytes + Alignment - 1) & ~(Alignment - 1); }

  static void split(Block* block, const size_t bytes) {
    if (block->bytes - bytes < sizeof(Block) + Alignment) return;
    Block* next = block->next;
    auto* tail =
        new (reinterpret_cast<uint8_t*>(block + 1) + bytes) Block{block->bytes - bytes - sizeof(Block), next, true};
    if (next && next->free) {
      tail->bytes += sizeof(Block) + next->bytes;
      tail->next = next->next;
    }
    block->next = tail;
    block->bytes = bytes;
  }

  Block* blocks_ = nullptr;
  size_t lastFailedRequest_ = 0;
};
