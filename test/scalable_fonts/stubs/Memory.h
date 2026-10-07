#pragma once
// Keep the real fallible allocator; only control its preflight heap snapshot.
#define byteHeapSnapshot hostByteHeapSnapshot
#include "../../../lib/Memory/Memory.h"
#undef byteHeapSnapshot
inline ByteHeapSnapshot testHeap{SIZE_MAX, SIZE_MAX, SIZE_MAX};
inline ByteHeapSnapshot byteHeapSnapshot(MemoryPool) { return testHeap; }
