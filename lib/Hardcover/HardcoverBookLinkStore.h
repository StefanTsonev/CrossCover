#pragma once

#include <string>
#include <vector>

struct HardcoverBookLink {
  std::string path;
  int bookId = 0;
  int lastSyncedProgress = -1;
  std::string title;
  int pendingStatusId = 0;
  int pendingProgress = -1;
  int pendingRating = 0;
  std::string pendingLookup;
  std::string pendingLookupAuthor;
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
  bool getPending(std::vector<HardcoverBookLink>& out) const;
  bool setLink(const std::string& path, int bookId, const std::string& title) const;
  bool queueStatus(const std::string& path, int statusId) const;
  bool queueProgress(const std::string& path, int progressPercent) const;
  bool queueRating(const std::string& path, int rating) const;
  bool queueLookup(const std::string& path, const std::string& title, const std::string& author,
                   const std::string& query) const;
  bool clearPendingStatus(const std::string& path) const;
  bool clearPendingProgress(const std::string& path) const;
  bool clearPendingRating(const std::string& path) const;
  bool clearPendingLookup(const std::string& path) const;
  bool updateLastSyncedProgress(const std::string& path, int progressPercent) const;
};

#define HARDCOVER_LINKS HardcoverBookLinkStore::getInstance()
