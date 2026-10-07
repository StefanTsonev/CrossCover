// Compiled against the real patched SdFat FsCache.cpp by test_sdfat_patches.py.
#include <common/FsCache.h>

#include <array>
#include <cassert>
#include <cstring>

static_assert(USE_SEPARATE_FAT_CACHE == EXPECTED_FAT_CACHE);

class FaultDevice : public FsBlockDeviceInterface {
 public:
  std::array<std::array<uint8_t, 512>, 4> sectors{};
  int failReadBytes = -1;
  bool failWrite = false;
  unsigned reads = 0;
  bool isBusy() override { return false; }
  bool syncDevice() override { return true; }
  Sector_t sectorCount() override { return sectors.size(); }
  bool readSector(Sector_t sector, uint8_t* dst) override {
    ++reads;
    const bool failed = failReadBytes >= 0;
    std::memcpy(dst, sectors.at(sector).data(), failed ? failReadBytes : 512);
    failReadBytes = -1;
    return !failed;
  }
  bool readSectors(Sector_t sector, uint8_t* dst, size_t count) override {
    for (size_t i = 0; i < count; ++i)
      if (!readSector(sector + i, dst + i * 512)) return false;
    return true;
  }
  bool writeSector(Sector_t sector, const uint8_t* src) override {
    if (failWrite) return false;
    std::memcpy(sectors.at(sector).data(), src, 512);
    return true;
  }
  bool writeSectors(Sector_t sector, const uint8_t* src, size_t count) override {
    for (size_t i = 0; i < count; ++i)
      if (!writeSector(sector + i, src + i * 512)) return false;
    return true;
  }
};

int main() {
  for (int failedBytes = 0; failedBytes <= 512; ++failedBytes) {
    FaultDevice device;
    device.sectors[0].fill(0x35);
    device.sectors[1].fill(0x9a);
    FsCache cache;
    cache.init(&device);
    assert(cache.prepare(0, FsCache::CACHE_FOR_READ));
    device.failReadBytes = failedBytes;
    assert(!cache.prepare(1, FsCache::CACHE_FOR_READ));
    std::array<uint8_t, 512> actual{};
    assert(cache.cacheSafeRead(0, actual.data()));
    assert(actual == device.sectors[0]);
    auto* data = cache.prepare(0, FsCache::CACHE_FOR_WRITE);
    assert(data && std::memcmp(data, device.sectors[0].data(), 512) == 0);
    data[0] = 0x77;
    assert(cache.sync());
    assert(device.sectors[0][0] == 0x77 && device.sectors[0][511] == 0x35);
  }
  // Failed writeback must keep the dirty old sector available for retry.
  FaultDevice device;
  FsCache cache;
  cache.init(&device);
  auto* data = cache.prepare(0, FsCache::CACHE_FOR_WRITE);
  assert(data);
  data[0] = 0x42;
  device.failWrite = true;
  const auto readsBefore = device.reads;
  assert(!cache.prepare(1, FsCache::CACHE_FOR_READ));
  assert(device.reads == readsBefore);
  assert(cache.prepare(0, FsCache::CACHE_FOR_READ)[0] == 0x42);
  device.failWrite = false;
  assert(cache.sync());
  assert(device.sectors[0][0] == 0x42);
}
