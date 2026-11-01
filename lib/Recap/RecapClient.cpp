#include "RecapClient.h"

#include <ArduinoJson.h>
#include <Epub.h>
#include <HalClock.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <ZipFile.h>

#include "ContentHash.h"
#include "RecapDefaults.h"
#include "RecapSession.h"
#ifndef SIMULATOR
#include <esp_system.h>
#endif
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
constexpr const char* REQUEST = "/.crosspoint/recap-request.json";
constexpr const char* CONFIG = "/.crosspoint/recap.json";
constexpr size_t MAX_REQUEST = 16384;
constexpr size_t MAX_RESPONSE = 8192;
bool read(const char* path, std::string& out, size_t max) {
  FsFile file;
  if (!Storage.openFileForRead("RECAP", path, file)) return false;
  if (file.size() > max) {
    file.close();
    LOG_ERR("RECAP", "File exceeds size limit");
    return false;
  }
  out.clear();
  out.reserve(file.size());
  char buffer[128];
  while (file.available()) {
    const int n = file.read(buffer, sizeof(buffer));
    if (n <= 0) {
      file.close();
      LOG_ERR("RECAP", "File read failed");
      return false;
    }
    out.append(buffer, n);
  }
  file.close();
  return true;
}
std::string cachePath(const std::string& request) {
  // Stable 64-bit filename hash on both C3 and desktop; not an auth mechanism.
  uint64_t hash = UINT64_C(14695981039346656037);
  for (const unsigned char byte : request) {
    hash ^= byte;
    hash *= UINT64_C(1099511628211);
  }
  char name[17];
  snprintf(name, sizeof(name), "%016llx", static_cast<unsigned long long>(hash));
  return std::string("/.crosspoint/recap-") + name + ".json";
}
bool write(const std::string& path, const JsonDocument& doc) {
  const std::string temp = path + ".part";
  FsFile file;
  if (!Storage.openFileForWrite("RECAP", temp, file)) return false;
  const bool ok = serializeJson(doc, file) == measureJson(doc);
  file.flush();
  file.close();
  if (!ok || !Storage.rename(temp.c_str(), path.c_str())) {
    LOG_ERR("RECAP", "Could not publish metadata/cache");
    Storage.remove(temp.c_str());
    return false;
  }
  return true;
}
struct TemporaryPlan {
  static constexpr const char* PATH = "/.crosspoint/recap-upload.json";
  ~TemporaryPlan() { Storage.remove(PATH); }
};
std::string responseStatus(int http, const std::string& response) {
  JsonDocument doc;
  if (http < 0 || deserializeJson(doc, response)) return "network_error";
  const std::string status = doc["status"] | "network_error";
  return status.size() <= 32 ? status : "invalid_response";
}
Recap::RequestSource fileSource(FsFile& file) {
  return {&file, [](void* p, uint8_t* data, size_t size) { return static_cast<FsFile*>(p)->read(data, size); }};
}
struct MemorySource {
  const uint8_t* data;
  size_t length;
  size_t position = 0;
  static int read(void* p, uint8_t* data, size_t size) {
    auto& self = *static_cast<MemorySource*>(p);
    const size_t n = std::min(size, self.length - self.position);
    if (n) std::memcpy(data, self.data + self.position, n);
    self.position += n;
    return static_cast<int>(n);
  }
};
std::string uploadBook(Recap::Session& session, JsonDocument& input, Recap::Progress progress, void* context) {
  const uint32_t started = millis();
  JsonArrayConst paths = input["members"].as<JsonArrayConst>();
  const size_t count = paths.size();
  if (count < 3 || count > 256) return "unsupported_book";
  // At most 256 fixed ZIP descriptors (~6 KiB), allocated once. Do not put a
  // chapter list on the task stack or retain decompressed chapters in RAM.
  auto entries = makeUniqueNoThrow<ZipFile::RawEntry[]>(count);
  if (!entries) {
    LOG_ERR("RECAP", "OOM allocating raw ZIP member descriptors");
    return "low_memory";
  }
  struct Scan {
    JsonArrayConst paths;
    ZipFile::RawEntry* entries;
  } scan{paths, entries.get()};
  const std::string bookPath = input["path"] | "";
  bool scanned = false;
  {
    ZipFile zip(bookPath);
    scanned = zip.visitRawEntries(
        &scan,
        [](void* context, std::string_view name, const ZipFile::RawEntry& entry) {
          auto& scan = *static_cast<Scan*>(context);
          for (size_t i = 0; i < scan.paths.size(); ++i) {
            const char* path = scan.paths[i] | "";
            if (name != path) continue;
            if (scan.entries[i].offset || (entry.method != 0 && entry.method != 8) || !entry.compressed ||
                entry.compressed > 1024 * 1024 || entry.size > 1024 * 1024)
              return false;
            scan.entries[i] = entry;
          }
          return true;
        },
        [](void* context, std::string_view name) {
          const auto& scan = *static_cast<Scan*>(context);
          for (JsonVariantConst path : scan.paths)
            if (name == path.as<const char*>()) return true;
          return false;
        });
    zip.close();
  }
  if (!scanned) {
    LOG_ERR("RECAP", "Could not validate ZIP directory/local headers");
    return "unsupported_book";
  }
  FsFile book;
  if (!Storage.openFileForRead("RECAP", bookPath, book)) return "storage_error";
  const uint64_t originalSize = book.size();
  JsonDocument manifest;
  JsonArray members = manifest["entries"].to<JsonArray>();
  size_t selected = 0;
  for (size_t i = 0; i < count; ++i) {
    const auto& entry = entries[i];
    if (!entry.offset || !book.seekSet(entry.offset) || entry.compressed > 8 * 1024 * 1024 - selected) {
      LOG_ERR("RECAP", "Missing or oversized selected ZIP member=%u", unsigned(i));
      book.close();
      return "unsupported_book";
    }
    selected += entry.compressed;
    Recap::ContentHash hash;
    for (size_t remaining = entry.compressed; remaining;) {
      const size_t want = std::min(Recap::Session::BUFFER_SIZE, remaining);
      const int n = book.read(session.scratch(), want);
      if (n != static_cast<int>(want)) {
        LOG_ERR("RECAP", "Selected member read failed");
        book.close();
        return "storage_error";
      }
      hash.add(session.scratch(), want);
      remaining -= want;
      yield();
    }
    char sha[65];
    hash.finish(sha);
    JsonObject member = members.add<JsonObject>();
    member["name"] = paths[i].as<const char*>();
    member["method"] = entry.method;
    member["compressed"] = entry.compressed;
    member["size"] = entry.size;
    member["crc"] = entry.crc;
    member["sha"] = sha;
  }
  book.close();
  const size_t manifestSize = measureJson(manifest);
  if (manifest.overflowed() || manifestSize > 65536) return "unsupported_book";
  TemporaryPlan cleanup;
  // Serialize the small descriptor list to temporary SD metadata, then free
  // JSON allocations before TLS. This is not another copy of the book.
  if (Storage.exists(TemporaryPlan::PATH) && !Storage.remove(TemporaryPlan::PATH)) return "storage_error";
  if (!write(TemporaryPlan::PATH, manifest)) return "storage_error";
  manifest.clear();
  input.remove("members");
  FsFile metadata;
  if (!Storage.openFileForRead("RECAP", TemporaryPlan::PATH, metadata)) return "storage_error";
  std::string response;
  const int planned = session.request("POST", "/v3/plan", fileSource(metadata), manifestSize, response);
  metadata.close();
  if (planned != 200) return responseStatus(planned, response);
  JsonDocument state;
  if (deserializeJson(state, response) || !state["offset"].is<size_t>() || !state["total"].is<size_t>() ||
      !state["bookKey"].is<const char*>() || !state["missing"].is<const char*>())
    return "invalid_response";
  const std::string key = state["bookKey"].as<const char*>(), mask = state["missing"].as<const char*>();
  if (key.size() != 64 || key.find_first_not_of("0123456789abcdef") != std::string::npos || mask.size() != count ||
      mask.find_first_not_of("01") != std::string::npos)
    return "invalid_response";
  size_t total = 0, needed = 0;
  for (size_t i = 0; i < count; ++i)
    if (mask[i] == '1') {
      total += entries[i].compressed;
      entries[needed++] = entries[i];
    }
  size_t offset = state["offset"].as<size_t>();
  if (total != state["total"].as<size_t>() || offset > total || (offset != total && offset % 16384))
    return "invalid_response";
  state.clear();
  LOG_INF("RECAP", "Selected EPUB members=%u original=%llu compressed=%u missing=%u resume=%u preparation=%lu ms",
          unsigned(count), static_cast<unsigned long long>(originalSize), unsigned(selected), unsigned(total),
          unsigned(offset), static_cast<unsigned long>(millis() - started));
  if (progress && total) progress(context, offset, total);
  if (offset < total) {
    if (!Storage.openFileForRead("RECAP", bookPath, book)) return "storage_error";
    struct Ranges {
      FsFile* book;
      const ZipFile::RawEntry* entries;
      size_t count, index, within, base, total;
      Recap::Progress progress;
      void* context;
      uint32_t lastUpdate, readMicros;
    } ranges{&book, entries.get(), needed, 0, offset, offset, total, progress, context, millis(), 0};
    while (ranges.index < needed && ranges.within >= entries[ranges.index].compressed)
      ranges.within -= entries[ranges.index++].compressed;
    const Recap::RequestSource source{
        &ranges,
        [](void* p, uint8_t* data, size_t size) {
          auto& r = *static_cast<Ranges*>(p);
          if (r.index >= r.count) return 0;
          const auto& entry = r.entries[r.index];
          const size_t want = std::min(size, size_t(entry.compressed) - r.within);
          const uint32_t started = micros();
          if (r.book->position() != entry.offset + r.within && !r.book->seekSet(entry.offset + r.within)) return -1;
          const int n = r.book->read(data, want);
          r.readMicros += micros() - started;
          if (n > 0) {
            r.within += n;
            if (r.within == entry.compressed) {
              ++r.index;
              r.within = 0;
            }
          }
          return n;
        },
        [](void* p, size_t sent) {
          auto& r = *static_cast<Ranges*>(p);
          if (r.progress && (r.base + sent == r.total || millis() - r.lastUpdate >= 1000)) {
            r.lastUpdate = millis();
            r.progress(r.context, r.base + sent, r.total);
          }
        }};
    while (offset < total) {
      const size_t length = std::min(size_t(1024 * 1024), total - offset);
      ranges.base = offset;
      const int status =
          session.request("PUT", "/v3/book/" + key + "/stream/" + std::to_string(offset) + "/" + std::to_string(length),
                          source, length, response, true);
      if (status != 200) {
        book.close();
        return responseStatus(status, response);
      }
      if (deserializeJson(state, response) || !state["offset"].is<size_t>() ||
          state["offset"].as<size_t>() != offset + length) {
        book.close();
        return "invalid_response";
      }
      offset += length;
    }
    LOG_INF("RECAP", "Selected member upload complete: bytes=%u SD read=%lu ms", unsigned(total),
            static_cast<unsigned long>(ranges.readMicros / 1000));
    book.close();
  }
  input["bookKey"] = key;
  input.remove("path");
  input.remove("contentSize");
  return "ready";
}
}  // namespace

bool Recap::prepare(Epub& epub, int currentSpine) {
  if (Storage.exists(REQUEST) && !Storage.remove(REQUEST)) {
    LOG_ERR("RECAP", "Could not clear previous handoff");
    return false;
  }
  const int count = epub.getTocItemsCount();
  if (count <= 0 || count > 128) {
    LOG_ERR("RECAP", "Unsupported TOC count=%d currentSpine=%d", count, currentSpine);
    return false;
  }
  int first = currentSpine, last = currentSpine;
  if (epub.hasChapterGroups() && !epub.resolveChapterGroupRange(currentSpine, first, last)) {
    LOG_ERR("RECAP", "Could not resolve chapter group: spine=%d", currentSpine);
    return false;
  }
  JsonDocument doc;
  doc["protocol"] = 3;
  doc["path"] = epub.getPath();
  // Include layout identity in the local cache key, without sending the TOC.
  doc["contentSize"] = epub.getBookSize();
  int through = -1, previous = -1, previousToc = -1, chapterStart = -1;
  std::string lastTitle, previousTitle;
  for (int i = 0; i < count; ++i) {
    const auto toc = epub.getTocItem(i);
    // Leading links to material outside the reading spine (for example a
    // cover or contents page) cannot establish a chapter boundary. Ignore
    // them until the first mapped chapter; unresolved links later remain
    // unsupported because they could hide a boundary within the story.
    if (toc.spineIndex < 0 && previous < 0) {
      LOG_INF("RECAP", "Skipping leading non-spine TOC entry=%d", i);
      continue;
    }
    // Anchor-level TOCs cannot be safely mapped to the current spine alone.
    if (toc.spineIndex < 0 || toc.spineIndex <= previous || toc.title.empty() || toc.title.size() > 160) {
      LOG_ERR("RECAP", "Unsupported TOC entry=%d spine=%d previous=%d titleBytes=%u currentSpine=%d groupFirst=%d", i,
              toc.spineIndex, previous, static_cast<unsigned>(toc.title.size()), currentSpine, first);
      return false;
    }
    previous = toc.spineIndex;
    // A chapter can span several spine files even without chapter-group data.
    // Find its TOC start first; never summarize the early files of that chapter.
    if (toc.spineIndex <= first) {
      chapterStart = toc.spineIndex;
      through = previousToc;
      lastTitle = previousTitle;
    }
    previousTitle = toc.title;
    previousToc = i;
  }
  if (through < 0 || chapterStart <= 0) {
    LOG_ERR("RECAP", "No completed chapter: currentSpine=%d groupFirst=%d chapterStart=%d through=%d", currentSpine,
            first, chapterStart, through);
    return false;
  }
  doc["cutoff"]["spine"] = chapterStart;
  doc["cutoff"]["href"] = epub.getSpineItem(chapterStart).href;
  doc["summarizeThroughChapter"] = through;
  doc["throughTitle"] = lastTitle;
  // Keep the actual OPF and container so the server independently validates
  // spine order and cutoff. Content after the current chapter is never copied.
  std::string package;
  if (!epub.getPackageDocumentPath(package) || chapterStart > 254) {
    LOG_ERR("RECAP", "Cannot prepare package/member list for cutoff=%d", chapterStart);
    return false;
  }
  JsonArray members = doc["members"].to<JsonArray>();
  members.add("META-INF/container.xml");
  members.add(package);
  for (int i = 0; i < chapterStart; ++i) members.add(epub.getSpineItem(i).href);

  if (doc.overflowed() || measureJson(doc) > MAX_REQUEST) {
    LOG_ERR("RECAP", "Chapter handoff exceeds metadata limit");
    return false;
  }
  const bool saved = write(REQUEST, doc);
  if (!saved) LOG_ERR("RECAP", "Could not save chapter handoff to SD");
  return saved;
}
bool Recap::hasRequest() { return Storage.exists(REQUEST); }
std::string Recap::scopeTitle() {
  FsFile file;
  if (!Storage.openFileForRead("RECAP", REQUEST, file)) return {};
  JsonDocument doc, filter;
  filter["throughTitle"] = true;
  const auto error = deserializeJson(doc, file, DeserializationOption::Filter(filter));
  file.close();
  if (error) return {};
  return doc["throughTitle"] | "";
}
bool Recap::loadCached(std::string& summary) {
  std::string request, raw;
  if (!read(REQUEST, request, MAX_REQUEST)) return false;
  const auto path = cachePath(request);
  JsonDocument input, filter;
  filter["summarizeThroughChapter"] = true;
  if (deserializeJson(input, request, DeserializationOption::Filter(filter))) return false;
  const int through = input["summarizeThroughChapter"] | -1;
  // Release metadata before loading a response while the EPUB is still open.
  request.clear();
  request.shrink_to_fit();
  if (through < 0 || !read(path.c_str(), raw, MAX_RESPONSE)) return false;
  JsonDocument doc;
  if (deserializeJson(doc, raw) || !doc["throughChapter"].is<int>() || doc["throughChapter"].as<int>() != through ||
      doc["status"] != "ready" || !doc["summary"].is<const char*>())
    return false;
  summary = doc["summary"].as<const char*>();
  return !summary.empty() && summary.size() <= 4096;
}
std::string Recap::generate(Progress progress, void* context, uint32_t* retryAfter) {
  if (retryAfter) *retryAfter = 0;
  std::string request, configuration, ca;
  if (!read(REQUEST, request, MAX_REQUEST)) return "invalid_request";
  JsonDocument input;
  if (deserializeJson(input, request) || input["protocol"] != 3) return "invalid_request";
  const std::string destination = cachePath(request);
  // The parsed document owns its strings. Release the redundant handoff copy
  // (up to 16 KiB) before allocating member descriptors and starting TLS.
  request.clear();
  request.shrink_to_fit();
  const int through = input["summarizeThroughChapter"] | -1;
  if (through < 0) return "invalid_request";
  JsonDocument settings;
  // SD configuration is an optional override, not a first-use requirement.
  // An explicit but unreadable/invalid override must not silently use defaults.
  if (Storage.exists(CONFIG)) {
    if (!read(CONFIG, configuration, 2048) || deserializeJson(settings, configuration)) return "not_configured";
  }
  const std::string url = settings["url"] | RecapDefaults::URL;
  if (Storage.exists("/.crosspoint/recap-ca.pem")) {
    if (!read("/.crosspoint/recap-ca.pem", ca, 4096)) return "not_configured";
  } else {
    // Reuses the existing bounded CA string; the full EPUB is never buffered.
    ca = RecapDefaults::ROOT_CA;
  }
  std::string identity;
  if (!read("/.crosspoint/recap-device.json", identity, 256)) {
    char id[33];
#ifdef SIMULATOR
    snprintf(id, sizeof(id), "%08lx%08lx", static_cast<unsigned long>(millis()), static_cast<unsigned long>(rand()));
#else
    snprintf(id, sizeof(id), "%08lx%08lx%08lx%08lx", static_cast<unsigned long>(esp_random()),
             static_cast<unsigned long>(esp_random()), static_cast<unsigned long>(esp_random()),
             static_cast<unsigned long>(esp_random()));
#endif
    JsonDocument device;
    device["id"] = id;
    if (!write("/.crosspoint/recap-device.json", device)) return "storage_error";
    serializeJson(device, identity);
  }
  JsonDocument device;
  if (deserializeJson(device, identity)) return "not_configured";
  const std::string deviceId = device["id"] | "";
  settings.clear();
  configuration.clear();
  if (url.rfind("https://", 0) != 0 || url.size() > 256 || deviceId.empty() || deviceId.size() > 32 ||
      url.find_first_of("\r\n?#@") != std::string::npos || deviceId.find_first_of("\r\n") != std::string::npos)
    return "not_configured";
  if (ESP.getFreeHeap() < 55000 || ESP.getMaxAllocHeap() < 24000) return "low_memory";
#ifndef SIMULATOR
  if (!halClock.syncSystemTimeFromNTP()) return "network_error";
#endif
  Recap::Session session(url, ca, deviceId);
  if (!session.init()) return "low_memory";
  const auto uploaded = uploadBook(session, input, progress, context);
  if (uploaded != "ready") return uploaded;
  if (progress) progress(context, 0, 0);
  std::string payload;
  serializeJson(input, payload);
  input.clear();
  const std::string temp = destination + ".part";
  MemorySource source{reinterpret_cast<const uint8_t*>(payload.data()), payload.size()};
  std::string raw;
  const int status =
      session.request("POST", "/v3/recaps", {&source, MemorySource::read}, payload.size(), raw, false, MAX_RESPONSE);
  if (status < 0) {
    LOG_ERR("RECAP", "Incomplete recap response");
    return "network_error";
  }
  JsonDocument doc;
  if (deserializeJson(doc, raw)) {
    Storage.remove(temp.c_str());
    return "invalid_response";
  }
  const std::string result = doc["status"] | "invalid_response";
  if (status == 429 && result == "budget_exhausted") {
    if (retryAfter && doc["retryAfter"].is<uint32_t>()) {
      const uint32_t seconds = doc["retryAfter"].as<uint32_t>();
      if (seconds <= 604800) *retryAfter = seconds;
    }
    const char* kind = doc["quota"] | "unknown";
    if (std::strcmp(kind, "daily") == 0) return "quota_daily";
    if (std::strcmp(kind, "minute") == 0) return "quota_minute";
    return "budget_exhausted";
  }

  if (status == 200 && result == "ready" && doc["throughChapter"].is<int>() &&
      doc["throughChapter"].as<int>() == through && doc["summary"].is<const char*>() &&
      strlen(doc["summary"].as<const char*>()) > 0 && strlen(doc["summary"].as<const char*>()) <= 4096) {
    // SD rename does not replace an existing file. Existing ready caches are
    // normally returned offline before this network path is reached.
    if ((Storage.exists(destination.c_str()) && !Storage.remove(destination.c_str())) || !write(destination, doc)) {
      LOG_ERR("RECAP", "Could not publish validated recap");
      Storage.remove(temp.c_str());
      return "storage_error";
    }
    return "ready";
  }
  Storage.remove(temp.c_str());
  LOG_ERR("RECAP", "Request failed http=%d", status);
  return result != "ready" && result.size() <= 32 ? result : "invalid_response";
}
