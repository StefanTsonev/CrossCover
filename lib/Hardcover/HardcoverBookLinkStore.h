#pragma once

#include <string>

struct HardcoverBookLink {
  std::string path;
  int bookId = 0;
  int lastSyncedProgress = -1;
  bool autoSync = false;
  std::string title;
};

class HardcoverBookLinkStore {
 private:
  static HardcoverBookLinkStore instance;

  HardcoverBookLinkStore() = default;

 public:
  HardcoverBookLinkStore(const HardcoverBookLinkStore&) = delete;
  HardcoverBookLinkStore& operator=(const HardcoverBookLinkStore&) = delete;

  static HardcoverBookLinkStore& getInstance() { return instance; }

  bool getLink(const std::string& path, HardcoverBookLink& out) const;
  bool setLink(const std::string& path, int bookId, const std::string& title) const;
  bool setAutoSync(const std::string& path, bool enabled) const;
  bool updateLastSyncedProgress(const std::string& path, int progressPercent) const;
};

#define HARDCOVER_LINKS HardcoverBookLinkStore::getInstance()
