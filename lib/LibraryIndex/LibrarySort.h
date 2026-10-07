#pragma once

// Bounded-memory sorting for the Library builder.
//
// Every Library order (title, author, series, arrival, ...) sorts one 16-byte
// entry per book. Holding all of them in RAM is what used to cap the C3 at
// 4,096 books. ExternalSorter keeps a fixed buffer instead: when it fills, the
// buffer is sorted and written to the card as a run, and finish() merges the
// runs back in one pass, keeping a small slice of each run in the same buffer.
// RAM use is the buffer, whatever the library size. On PSRAM devices the
// buffer covers the whole input, so nothing spills and the sort stays in RAM.
//
// Keys are compared 12 bytes at a time. When the full key is longer, equal
// prefixes are refined by loading the next 12 bytes from the caller's source
// (the staged book), so only ties cost card reads. A prefix of all 0xFF bytes
// means "no value": those entries are ordered by `ordinal` without refinement.

#include <HalStorage.h>
#include <Memory.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace library {

// Per-book working arrays. On PSRAM devices they are placed in PSRAM so a large
// library neither fragments nor competes for the internal heap the reader
// needs; elsewhere, or if PSRAM is exhausted, they come from the default heap.
// Zero-filled. Only trivially copyable element types are stored, so raw byte
// storage needs no construction.
template <typename T>
class BookArray {
  static_assert(std::is_trivially_copyable_v<T> && std::is_trivially_destructible_v<T>,
                "BookArray holds plain records only");

 public:
  bool allocate(const size_t count) {
    const size_t bytes = (count == 0 ? 1 : count) * sizeof(T);
    storage.reset();
    if (psramHeapAvailable()) storage = makeAlignedByteBufferNoThrow(bytes, MemoryPool::Psram);
    if (!storage) storage = makeAlignedByteBufferNoThrow(bytes);
    if (storage) memset(storage.get(), 0, bytes);
    return static_cast<bool>(storage);
  }
  void reset() { storage.reset(); }
  T* get() const { return reinterpret_cast<T*>(storage.get()); }
  T& operator[](const size_t index) const { return get()[index]; }
  explicit operator bool() const { return static_cast<bool>(storage); }

 private:
  HeapByteBuffer storage;
};

inline constexpr size_t SORT_SEGMENT_BYTES = 12;

struct SortEntry {
  char key[SORT_SEGMENT_BYTES];
  // Breaks ties, and is the value most passes emit (a title position).
  uint16_t ordinal;
  // The staged book whose text the key came from; passed back to the loader.
  uint16_t source;
};
static_assert(sizeof(SortEntry) == 16, "sort entries must tile a 512-byte sector");

// Write key bytes [offset, offset + 12) for `source` into `segment`, zero-padded
// past the end of the value, and set `valueBytes` to the whole key's length.
// Refinement stops once every compared value has ended, so equal values (a
// shared genre, say) cost one load each rather than one per segment of the
// maximum key length. Return false on I/O failure.
using SortSegmentLoader = bool (*)(void* context, uint16_t source, size_t offset, char* segment, size_t& valueBytes);
// Receives entries in sorted order. Return false to abort the sort.
using SortEmitter = bool (*)(void* context, const SortEntry& entry);

struct SortConfig {
  const char* runPath = nullptr;
  // Full key length. Keys longer than one segment need `load`.
  size_t keyBytes = SORT_SEGMENT_BYTES;
  SortSegmentLoader load = nullptr;
  void* loadContext = nullptr;
  // Called between bounded units of work; `stopped` aborts the sort.
  void (*service)() = nullptr;
  bool (*stopped)() = nullptr;
};

// The default C3 buffer: 2,048 entries, 32 KiB. 32,767 books make 16 runs, each
// still merged from a 128-entry (2 KiB) slice.
inline constexpr uint16_t SORT_RUN_ENTRIES = 2048;
// Allocation retries halve the buffer down to this before giving up.
inline constexpr uint16_t SORT_MIN_RUN_ENTRIES = 256;

// Tests only: cap the in-RAM buffer to force spilled runs. 0 restores the default.
void setSortRunCapacityForTesting(uint16_t entries);

// Fully order `count` entries in place: by key prefix, then by refined key
// segments for equal prefixes, then by ordinal. Prefixes are left intact.
bool sortEntries(SortEntry* entries, uint16_t count, const SortConfig& config);

class ExternalSorter {
 public:
  ExternalSorter() = default;
  ~ExternalSorter();
  ExternalSorter(const ExternalSorter&) = delete;
  ExternalSorter& operator=(const ExternalSorter&) = delete;

  // `expected` sizes the buffer; adding more entries than that is allowed.
  bool begin(const SortConfig& config, uint16_t expected);
  bool add(const SortEntry& entry);
  // Emits every added entry in order, then releases the buffer and run file.
  bool finish(SortEmitter emit, void* context);

  bool spilled() const { return runCount > 0; }

 private:
  struct Head;
  bool spillRun();
  bool merge(SortEmitter emit, void* context);
  const char* segmentAt(Head& head, size_t offset, char* scratch);
  int compare(Head& a, Head& b);
  void release();

  SortConfig config;
  BookArray<SortEntry> buffer;
  uint16_t capacity = 0;
  uint16_t used = 0;
  uint16_t runCount = 0;
  uint32_t total = 0;
  HalFile runs;
  bool failed = false;
};

}  // namespace library
