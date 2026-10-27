#pragma once

#include <string>
#include <string_view>
#include <vector>

struct HardcoverLibraryBook;

struct HardcoverBookLink {
  std::string path;
  int bookId = 0;
  int editionId = 0;
  int editionPages = 0;
  int lastSyncedProgress = -1;
  int lastSyncedStatusId = 0;
  float lastSyncedRating = -1;
  std::string remoteTitle;
  std::string title;
  int pendingStatusId = 0;
  int pendingProgress = -1;
  float pendingRating = 0;
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
  bool getPending(std::vector<HardcoverBookLink>& out, std::string_view onlyPath = {}) const;
  bool setLink(const std::string& path, int bookId, const std::string& title, int editionId = 0,
               int editionPages = 0) const;
  bool queueStatus(const std::string& path, int statusId) const;
  bool queueFinish(const std::string& path) const;
  bool queueProgress(const std::string& path, int progressPercent) const;
  bool queueRating(const std::string& path, float rating) const;
  bool queueLookup(const std::string& path, const std::string& title, const std::string& author,
                   const std::string& query) const;
  bool clearPendingStatus(const std::string& path) const;
  bool clearPendingProgress(const std::string& path) const;
  bool clearPendingRating(const std::string& path) const;
  bool clearPendingLookup(const std::string& path) const;
  bool updateLastSyncedProgress(const std::string& path, int progressPercent) const;
  bool confirmStatus(const std::string& path, int statusId) const;
  bool confirmProgress(const std::string& path, int progressPercent) const;
  bool confirmRating(const std::string& path, float rating) const;
  bool prepareBook(const std::string& path, const std::string& title) const;
  bool clearPending(const std::string& path) const;
  bool cacheLibrary(const std::vector<HardcoverLibraryBook>& books) const;
};

#define HARDCOVER_LINKS HardcoverBookLinkStore::getInstance()
