#include "HardcoverBookLinkStore.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <PersistableStore.h>

#include <algorithm>
#include <utility>

#include "HardcoverClient.h"

HardcoverBookLinkStore HardcoverBookLinkStore::instance;

namespace {
constexpr char HARDCOVER_LINKS_JSON[] = "/.crosspoint/hardcover_links.json";

bool loadLinksDocument(JsonDocument& doc) {
  const std::string backupPath = std::string(HARDCOVER_LINKS_JSON) + ".bak";
  if (!Storage.exists(HARDCOVER_LINKS_JSON) && !Storage.exists(backupPath.c_str())) return true;
  if (!PersistableStoreBase::readDocFromFile(HARDCOVER_LINKS_JSON, doc)) return false;
  if (!doc["links"].is<JsonArrayConst>()) {
    LOG_ERR("HDL", "Link JSON is missing its links array");
    return false;
  }
  return true;
}

bool saveLinksDocument(JsonDocument& doc) {
  return PersistableStoreBase::writeDocToFileAtomically(HARDCOVER_LINKS_JSON, doc);
}

JsonArray ensureLinksArray(JsonDocument& doc) {
  JsonArray links = doc["links"].as<JsonArray>();
  if (links.isNull()) {
    links = doc["links"].to<JsonArray>();
  }
  return links;
}

JsonObject findLink(JsonArray links, const std::string& path) {
  for (JsonObject link : links) {
    const char* storedPath = link["path"] | "";
    if (path == storedPath) return link;
  }
  return JsonObject();
}
}  // namespace

bool HardcoverBookLinkStore::getLink(const std::string& path, HardcoverBookLink& out) const {
  JsonDocument doc;
  if (!loadLinksDocument(doc)) return false;

  JsonArray links = doc["links"].as<JsonArray>();
  for (JsonObject link : links) {
    const char* storedPath = link["path"] | "";
    if (path == storedPath) {
      out.path = storedPath;
      out.bookId = link["bookId"] | 0;
      out.editionId = link["editionId"] | 0;
      out.editionPages = link["editionPages"] | 0;
      out.lastSyncedProgress = link["lastSyncedProgress"] | -1;
      out.lastSyncedStatusId = link["lastSyncedStatusId"] | 0;
      out.lastSyncedRating = link["lastSyncedRating"] | -1.0f;
      out.remoteTitle = link["remoteTitle"] | std::string{};
      out.title = link["title"] | std::string("");
      out.pendingStatusId = link["pendingStatusId"] | 0;
      out.pendingProgress = link["pendingProgress"] | -1;
      out.pendingRating = link["pendingRating"] | 0.0f;
      out.pendingLookup = link["pendingLookup"] | std::string("");
      out.pendingLookupAuthor = link["pendingLookupAuthor"] | std::string("");
      return true;  // An unlinked record may still have a queued lookup.
    }
  }
  return false;
}

bool HardcoverBookLinkStore::getPending(std::vector<HardcoverBookLink>& out, std::string_view onlyPath) const {
  out.clear();
  JsonDocument doc;
  if (!loadLinksDocument(doc)) return false;

  JsonArray links = doc["links"].as<JsonArray>();
  out.reserve(onlyPath.empty() ? 8 : 1);
  for (JsonObject link : links) {
    const char* path = link["path"] | "";
    if (!onlyPath.empty() && onlyPath != path) continue;
    HardcoverBookLink pending;
    pending.path = link["path"] | std::string("");
    pending.bookId = link["bookId"] | 0;
    pending.editionId = link["editionId"] | 0;
    pending.editionPages = link["editionPages"] | 0;
    pending.lastSyncedProgress = link["lastSyncedProgress"] | -1;
    pending.lastSyncedStatusId = link["lastSyncedStatusId"] | 0;
    pending.lastSyncedRating = link["lastSyncedRating"] | -1.0f;
    pending.title = link["title"] | std::string("");
    pending.pendingStatusId = link["pendingStatusId"] | 0;
    pending.pendingProgress = link["pendingProgress"] | -1;
    pending.pendingRating = link["pendingRating"] | 0.0f;
    pending.pendingLookup = link["pendingLookup"] | std::string("");
    pending.pendingLookupAuthor = link["pendingLookupAuthor"] | std::string("");
    if (pending.pendingStatusId > 0 || pending.pendingProgress >= 0 || pending.pendingRating > 0 ||
        !pending.pendingLookup.empty()) {
      out.push_back(std::move(pending));
    }
  }
  return true;
}

bool HardcoverBookLinkStore::setLink(const std::string& path, int bookId, const std::string& title, const int editionId,
                                     const int editionPages) const {
  if (path.empty() || bookId <= 0 || editionId < 0 || editionPages < 0) return false;
  JsonDocument doc;
  if (!loadLinksDocument(doc)) {
    LOG_ERR("HDL", "Refusing to replace unreadable link JSON");
    return false;
  }

  JsonArray links = ensureLinksArray(doc);
  JsonObject link = findLink(links, path);
  if (!link.isNull()) {
    if ((link["editionId"] | 0) != editionId) link["lastSyncedProgress"] = -1;
    if ((link["bookId"] | 0) != bookId) {
      link["lastSyncedProgress"] = -1;
      link["lastSyncedStatusId"] = 0;
      link["lastSyncedRating"] = -1;
      link["remoteTitle"] = "";
    }
    link["bookId"] = bookId;
    // A manual book-ID link has no edition. Clear a previous ISBN match so
    // progress cannot silently keep targeting the old edition.
    link["editionId"] = editionId;
    link["editionPages"] = editionPages;
    link["title"] = title;
    if (editionId > 0) link["remoteTitle"] = title;
    link["pendingLookup"] = "";
    link["pendingLookupAuthor"] = "";
    return saveLinksDocument(doc);
  }

  JsonObject newLink = links.add<JsonObject>();
  newLink["path"] = path;
  newLink["bookId"] = bookId;
  newLink["editionId"] = editionId;
  newLink["editionPages"] = editionPages;
  newLink["lastSyncedProgress"] = -1;
  newLink["title"] = title;
  newLink["remoteTitle"] = editionId > 0 ? title : "";
  newLink["pendingStatusId"] = 0;
  newLink["pendingProgress"] = -1;
  newLink["pendingRating"] = 0;
  newLink["pendingLookup"] = "";
  newLink["pendingLookupAuthor"] = "";

  return saveLinksDocument(doc);
}

namespace {
bool updatePendingInt(const std::string& path, const char* key, const int value) {
  JsonDocument doc;
  if (!loadLinksDocument(doc)) return false;
  JsonObject link = findLink(ensureLinksArray(doc), path);
  if (link.isNull()) return false;
  link[key] = value;
  return saveLinksDocument(doc);
}

bool queueDesiredInt(const std::string& path, const char* confirmedKey, const char* pendingKey, int value,
                     int clearedValue) {
  JsonDocument doc;
  if (!loadLinksDocument(doc)) return false;
  JsonObject link = findLink(ensureLinksArray(doc), path);
  if (link.isNull()) return false;
  const int desired = (link[confirmedKey] | (clearedValue - 1)) == value ? clearedValue : value;
  if ((link[pendingKey] | clearedValue) == desired) return true;
  link[pendingKey] = desired;
  return saveLinksDocument(doc);
}

bool confirmPendingInt(const std::string& path, const char* confirmedKey, const char* pendingKey, int value,
                       int clearedValue) {
  JsonDocument doc;
  if (!loadLinksDocument(doc)) return false;
  JsonObject link = findLink(ensureLinksArray(doc), path);
  if (link.isNull()) return false;
  link[confirmedKey] = value;
  if ((link[pendingKey] | clearedValue) == value) link[pendingKey] = clearedValue;
  return saveLinksDocument(doc);
}

}  // namespace

bool HardcoverBookLinkStore::confirmStatus(const std::string& path, int statusId) const {
  return confirmPendingInt(path, "lastSyncedStatusId", "pendingStatusId", statusId, 0);
}

bool HardcoverBookLinkStore::confirmProgress(const std::string& path, int progressPercent) const {
  return confirmPendingInt(path, "lastSyncedProgress", "pendingProgress", progressPercent, -1);
}

bool HardcoverBookLinkStore::confirmRating(const std::string& path, float rating) const {
  JsonDocument doc;
  if (!loadLinksDocument(doc)) return false;
  JsonObject link = findLink(ensureLinksArray(doc), path);
  if (link.isNull()) return false;
  link["lastSyncedRating"] = rating;
  if ((link["pendingRating"] | 0.0f) == rating) link["pendingRating"] = 0;
  return saveLinksDocument(doc);
}

bool HardcoverBookLinkStore::prepareBook(const std::string& path, const std::string& title) const {
  if (path.empty()) return false;
  JsonDocument doc;
  if (!loadLinksDocument(doc)) return false;
  JsonArray links = ensureLinksArray(doc);
  JsonObject link = findLink(links, path);
  if (!link.isNull()) return true;
  link = links.add<JsonObject>();
  link["path"] = path;
  link["title"] = title;
  link["bookId"] = 0;
  link["pendingProgress"] = -1;
  link["lastSyncedProgress"] = -1;
  return saveLinksDocument(doc);
}

bool HardcoverBookLinkStore::clearPending(const std::string& path) const {
  JsonDocument doc;
  if (!loadLinksDocument(doc)) return false;
  JsonObject link = findLink(ensureLinksArray(doc), path);
  if (link.isNull()) return false;
  link["pendingStatusId"] = 0;
  link["pendingProgress"] = -1;
  link["pendingRating"] = 0;
  link["pendingLookup"] = "";
  link["pendingLookupAuthor"] = "";
  return saveLinksDocument(doc);
}

bool HardcoverBookLinkStore::cacheLibrary(const std::vector<HardcoverLibraryBook>& books) const {
  JsonDocument doc;
  if (!loadLinksDocument(doc)) return false;
  bool changed = false;
  for (JsonObject link : doc["links"].as<JsonArray>()) {
    const int bookId = link["bookId"] | 0;
    if (bookId <= 0) continue;
    for (const auto& book : books) {
      if (book.bookId != bookId) continue;
      const int expectedEdition = link["editionId"] | 0;
      const bool sameEdition = expectedEdition == 0 || expectedEdition == book.editionId;
      int percent = book.pages > 0 && sameEdition
                        ? std::clamp((book.progressPages * 100 + book.pages / 2) / book.pages, 0, 100)
                        : -1;
      const int savedPercent = link["lastSyncedProgress"] | -1;
      // Preserve the exact local percentage when it maps to the same remote
      // page. Otherwise short editions would repeatedly queue rounding noise.
      if (book.pages > 0 && sameEdition && savedPercent >= 0 &&
          (book.pages * savedPercent + 50) / 100 == book.progressPages) {
        percent = savedPercent;
      }
      const char* cachedTitle = link["remoteTitle"] | "";
      if ((link["lastSyncedStatusId"] | 0) != book.statusId || (link["lastSyncedRating"] | -1.0f) != book.rating ||
          book.title != cachedTitle || (percent >= 0 && (link["lastSyncedProgress"] | -1) != percent)) {
        link["lastSyncedStatusId"] = book.statusId;
        link["lastSyncedRating"] = book.rating;
        link["remoteTitle"] = book.title;
        if (percent >= 0) link["lastSyncedProgress"] = percent;
        changed = true;
      }
      break;
    }
  }
  return !changed || saveLinksDocument(doc);
}

bool HardcoverBookLinkStore::queueStatus(const std::string& path, const int statusId) const {
  if (statusId < 1 || statusId > 5) return false;
  return queueDesiredInt(path, "lastSyncedStatusId", "pendingStatusId", statusId, 0);
}

bool HardcoverBookLinkStore::queueFinish(const std::string& path) const {
  return queueStatus(path, 3);  // Finishing a book must not overwrite reading progress.
}

bool HardcoverBookLinkStore::queueProgress(const std::string& path, int progressPercent) const {
  progressPercent = std::max(0, std::min(100, progressPercent));
  return queueDesiredInt(path, "lastSyncedProgress", "pendingProgress", progressPercent, -1);
}

bool HardcoverBookLinkStore::queueRating(const std::string& path, float rating) const {
  if (!(rating >= 0.5f && rating <= 5.0f) || static_cast<int>(rating * 2) != rating * 2) return false;
  JsonDocument doc;
  if (!loadLinksDocument(doc)) return false;
  JsonObject link = findLink(ensureLinksArray(doc), path);
  if (link.isNull()) return false;
  const float desired = (link["lastSyncedRating"] | -1.0f) == rating ? 0.0f : rating;
  if ((link["pendingRating"] | 0.0f) == desired) return true;
  link["pendingRating"] = desired;
  return saveLinksDocument(doc);
}

bool HardcoverBookLinkStore::queueLookup(const std::string& path, const std::string& title, const std::string& author,
                                         const std::string& query) const {
  JsonDocument doc;
  if (!loadLinksDocument(doc)) return false;
  JsonObject link = findLink(ensureLinksArray(doc), path);
  if (link.isNull()) {
    JsonArray links = ensureLinksArray(doc);
    link = links.add<JsonObject>();
    link["path"] = path;
    link["bookId"] = 0;
    link["editionId"] = 0;
    link["editionPages"] = 0;
    link["lastSyncedProgress"] = -1;
    link["title"] = title;
    link["pendingStatusId"] = 0;
    link["pendingProgress"] = -1;
    link["pendingRating"] = 0;
  }
  link["pendingLookup"] = query.empty() ? title : query;
  link["pendingLookupAuthor"] = author;
  return saveLinksDocument(doc);
}

bool HardcoverBookLinkStore::clearPendingStatus(const std::string& path) const {
  return updatePendingInt(path, "pendingStatusId", 0);
}

bool HardcoverBookLinkStore::clearPendingProgress(const std::string& path) const {
  return updatePendingInt(path, "pendingProgress", -1);
}

bool HardcoverBookLinkStore::clearPendingRating(const std::string& path) const {
  return updatePendingInt(path, "pendingRating", 0);
}

bool HardcoverBookLinkStore::clearPendingLookup(const std::string& path) const {
  JsonDocument doc;
  if (!loadLinksDocument(doc)) return false;
  JsonObject link = findLink(ensureLinksArray(doc), path);
  if (link.isNull()) return false;
  link["pendingLookup"] = "";
  link["pendingLookupAuthor"] = "";
  return saveLinksDocument(doc);
}

bool HardcoverBookLinkStore::updateLastSyncedProgress(const std::string& path, int progressPercent) const {
  if (progressPercent < 0) progressPercent = 0;
  if (progressPercent > 100) progressPercent = 100;

  JsonDocument doc;
  if (!loadLinksDocument(doc)) return false;

  JsonArray links = ensureLinksArray(doc);
  JsonObject link = findLink(links, path);
  if (!link.isNull()) {
    link["lastSyncedProgress"] = progressPercent;
    return saveLinksDocument(doc);
  }
  return false;
}
