#include "HardcoverBookLinkStore.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <PersistableStore.h>

#include <algorithm>
#include <utility>

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
      out.lastSyncedProgress = link["lastSyncedProgress"] | -1;
      out.title = link["title"] | std::string("");
      out.pendingStatusId = link["pendingStatusId"] | 0;
      out.pendingProgress = link["pendingProgress"] | -1;
      out.pendingRating = link["pendingRating"] | 0;
      out.pendingLookup = link["pendingLookup"] | std::string("");
      out.pendingLookupAuthor = link["pendingLookupAuthor"] | std::string("");
      return out.bookId > 0;
    }
  }
  return false;
}

bool HardcoverBookLinkStore::getPending(std::vector<HardcoverBookLink>& out) const {
  out.clear();
  JsonDocument doc;
  if (!loadLinksDocument(doc)) return false;

  JsonArray links = doc["links"].as<JsonArray>();
  out.reserve(8);
  for (JsonObject link : links) {
    HardcoverBookLink pending;
    pending.path = link["path"] | std::string("");
    pending.bookId = link["bookId"] | 0;
    pending.lastSyncedProgress = link["lastSyncedProgress"] | -1;
    pending.title = link["title"] | std::string("");
    pending.pendingStatusId = link["pendingStatusId"] | 0;
    pending.pendingProgress = link["pendingProgress"] | -1;
    pending.pendingRating = link["pendingRating"] | 0;
    pending.pendingLookup = link["pendingLookup"] | std::string("");
    pending.pendingLookupAuthor = link["pendingLookupAuthor"] | std::string("");
    if (pending.pendingStatusId > 0 || pending.pendingProgress >= 0 || pending.pendingRating > 0 ||
        !pending.pendingLookup.empty()) {
      out.push_back(std::move(pending));
    }
  }
  return true;
}

bool HardcoverBookLinkStore::setLink(const std::string& path, int bookId, const std::string& title) const {
  JsonDocument doc;
  if (!loadLinksDocument(doc)) {
    LOG_ERR("HDL", "Refusing to replace unreadable link JSON");
    return false;
  }

  JsonArray links = ensureLinksArray(doc);
  JsonObject link = findLink(links, path);
  if (!link.isNull()) {
    link["bookId"] = bookId;
    link["title"] = title;
    link["pendingLookup"] = "";
    link["pendingLookupAuthor"] = "";
    return saveLinksDocument(doc);
  }

  JsonObject newLink = links.add<JsonObject>();
  newLink["path"] = path;
  newLink["bookId"] = bookId;
  newLink["lastSyncedProgress"] = -1;
  newLink["title"] = title;
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

}  // namespace

bool HardcoverBookLinkStore::queueStatus(const std::string& path, const int statusId) const {
  return updatePendingInt(path, "pendingStatusId", statusId);
}

bool HardcoverBookLinkStore::queueProgress(const std::string& path, int progressPercent) const {
  progressPercent = std::max(0, std::min(100, progressPercent));
  return updatePendingInt(path, "pendingProgress", progressPercent);
}

bool HardcoverBookLinkStore::queueRating(const std::string& path, int rating) const {
  rating = std::max(0, std::min(5, rating));
  return updatePendingInt(path, "pendingRating", rating);
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
