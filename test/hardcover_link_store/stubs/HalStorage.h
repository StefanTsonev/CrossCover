#pragma once
#include "PersistableStore.h"
struct TestStorage {
  bool exists(const char*) const { return !HardcoverTestStorage::file.empty(); }
};
inline TestStorage Storage;
