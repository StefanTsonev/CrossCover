#include "HardcoverBookLinkStore.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

HardcoverBookLinkStore HardcoverBookLinkStore::instance;

namespace {
constexpr char HARDCOVER_LINKS_JSON[] = "/.crosspoint/hardcover_links.json";

bool loadLinksDocument(JsonDocument& doc) {
  if (!Storage.exists(HARDCOVER_LINKS_JSON)) return true;

  String json = Storage.readFile(HARDCOVER_LINKS_JSON);
  if (json.isEmpty()) return true;

  auto error = deserializeJson(doc, json.c_str());
  if (error) {
    LOG_ERR("HDL", "Link JSON parse error: %s", error.c_str());
    return false;
  }
  return true;
}

bool saveLinksDocument(JsonDocument& doc) {
  Storage.mkdir("/.crosspoint");
  String json;
  serializeJson(doc, json);
  return Storage.writeFile(HARDCOVER_LINKS_JSON, json);
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
}

bool HardcoverBookLinkStore::getLink(const std::string& path, HardcoverBookLink& out) const {
  if (!Storage.exists(HARDCOVER_LINKS_JSON)) return false;

  String json = Storage.readFile(HARDCOVER_LINKS_JSON);
  if (json.isEmpty()) return false;

  JsonDocument doc;
  auto error = deserializeJson(doc, json.c_str());
  if (error) {
    LOG_ERR("HDL", "Link JSON parse error: %s", error.c_str());
    return false;
  }

  JsonArray links = doc["links"].as<JsonArray>();
  for (JsonObject link : links) {
    const char* storedPath = link["path"] | "";
    if (path == storedPath) {
      out.path = storedPath;
      out.bookId = link["bookId"] | 0;
      out.lastSyncedProgress = link["lastSyncedProgress"] | -1;
      out.autoSync = link["autoSync"] | false;
      out.title = link["title"] | std::string("");
      return out.bookId > 0;
    }
  }
  return false;
}

bool HardcoverBookLinkStore::setLink(const std::string& path, int bookId, const std::string& title) const {
  Storage.mkdir("/.crosspoint");

  JsonDocument doc;
  if (!loadLinksDocument(doc)) {
    LOG_ERR("HDL", "Replacing unreadable link JSON");
    doc.clear();
  }

  JsonArray links = ensureLinksArray(doc);
  JsonObject link = findLink(links, path);
  if (!link.isNull()) {
    link["bookId"] = bookId;
    link["title"] = title;
    return saveLinksDocument(doc);
  }

  JsonObject newLink = links.add<JsonObject>();
  newLink["path"] = path;
  newLink["bookId"] = bookId;
  newLink["autoSync"] = false;
  newLink["lastSyncedProgress"] = -1;
  newLink["title"] = title;

  return saveLinksDocument(doc);
}

bool HardcoverBookLinkStore::setAutoSync(const std::string& path, bool enabled) const {
  JsonDocument doc;
  if (!loadLinksDocument(doc)) return false;

  JsonArray links = ensureLinksArray(doc);
  JsonObject link = findLink(links, path);
  if (!link.isNull()) {
    link["autoSync"] = enabled;
    return saveLinksDocument(doc);
  }
  return false;
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
