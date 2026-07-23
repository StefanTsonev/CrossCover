#include "ShadowLibraryClient.h"

#include "HttpDownloader.h"
#include <ArduinoJson.h>
#include <Logging.h>

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace {
constexpr size_t MAX_TAG_BYTES = 512;
constexpr size_t MAX_FIELD_BYTES = 192;

enum class Capture { NONE, TITLE, AUTHOR, PUBLISHER, INFO };

std::string absoluteUrl(const std::string& href) {
  if (href.starts_with("http://") || href.starts_with("https://")) return href;
  if (!href.empty() && href[0] == '/') return std::string(ShadowLibraryClient::BASE_URL) + href;
  return std::string(ShadowLibraryClient::BASE_URL) + "/" + href;
}

std::string attribute(const std::string& tag, const char* name) {
  const std::string key = std::string(name) + "=";
  size_t pos = tag.find(key);
  if (pos == std::string::npos) return {};
  pos += key.size();
  if (pos >= tag.size()) return {};
  const char quote = tag[pos];
  if (quote != '\'' && quote != '"') return {};
  const size_t end = tag.find(quote, pos + 1);
  return end == std::string::npos ? std::string{} : tag.substr(pos + 1, end - pos - 1);
}

bool hasClass(const std::string& tag, const char* className) {
  const std::string classes = attribute(tag, "class");
  if (classes.empty()) return false;
  std::string required = className;
  size_t start = 0;
  while (start < required.size()) {
    const size_t end = required.find(' ', start);
    const std::string token = required.substr(start, end == std::string::npos ? std::string::npos : end - start);
    if (!token.empty() && classes.find(token) == std::string::npos) return false;
    if (end == std::string::npos) break;
    start = end + 1;
  }
  return true;
}

std::string tagName(const std::string& tag) {
  size_t pos = tag.find_first_not_of("< /\t\r\n");
  if (pos == std::string::npos) return {};
  const size_t end = tag.find_first_of(" \t\r\n/>", pos);
  return tag.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
}

void appendText(std::string& target, const char* data, const size_t length) {
  for (size_t i = 0; i < length && target.size() < MAX_FIELD_BYTES; ++i) {
    const char c = data[i];
    if (std::isspace(static_cast<unsigned char>(c))) {
      if (!target.empty() && target.back() != ' ') target.push_back(' ');
    } else {
      target.push_back(c);
    }
  }
  while (!target.empty() && target.back() == ' ') target.pop_back();
}

void decodeBasicEntities(std::string& value) {
  struct Entity {
    const char* encoded;
    const char decoded;
  };
  constexpr Entity entities[] = {{"&amp;", '&'}, {"&quot;", '"'}, {"&#39;", '\''},
                                 {"&lt;", '<'},   {"&gt;", '>'}};
  for (const auto& entity : entities) {
    size_t pos = 0;
    while ((pos = value.find(entity.encoded, pos)) != std::string::npos) {
      value.replace(pos, std::char_traits<char>::length(entity.encoded), 1, entity.decoded);
      ++pos;
    }
  }
}

std::string encodeQuery(const std::string& query) {
  std::string encoded;
  encoded.reserve(query.size() + 8);
  for (const unsigned char c : query) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded.push_back(static_cast<char>(c));
    } else if (c == ' ') {
      encoded.push_back('+');
    } else {
      char escaped[4];
      snprintf(escaped, sizeof(escaped), "%%%02X", c);
      encoded += escaped;
    }
  }
  return encoded;
}

class SearchParser {
 public:
  SearchParser(ShadowLibraryBook* results, const size_t capacity) : results_(results), capacity_(capacity) {}

  bool write(const uint8_t* data, const size_t length) {
    for (size_t i = 0; i < length; ++i) {
      const char c = static_cast<char>(data[i]);
      if (inTag_) {
        if (tag_.size() < MAX_TAG_BYTES) tag_.push_back(c);
        if (c == '>') {
          processTag();
          tag_.clear();
          inTag_ = false;
        }
      } else if (c == '<') {
        inTag_ = true;
        tag_.clear();
        tag_.push_back(c);
      } else if (inCard_) {
        appendCaptured(&c, 1);
      }
    }
    return true;
  }

  void finish() {
    if (inCard_ && card_.detailUrl.size() > 0) finalizeCard();
  }

 private:
  void appendCaptured(const char* data, const size_t length) {
    switch (capture_) {
      case Capture::TITLE:
        appendText(card_.title, data, length);
        break;
      case Capture::AUTHOR:
        appendText(card_.author, data, length);
        break;
      case Capture::PUBLISHER:
        // Publisher is intentionally ignored; the e-reader only needs a
        // compact title/author row and keeping another string wastes heap.
        break;
      case Capture::INFO:
        appendText(info_, data, length);
        break;
      case Capture::NONE:
        break;
    }
  }

  void finalizeCard() {
    decodeBasicEntities(card_.title);
    decodeBasicEntities(card_.author);
    const std::string info = info_;
    if (!emittedCard_ && !card_.title.empty() && !card_.detailUrl.empty() &&
        (info.empty() || info.find("pdf") != std::string::npos || info.find("epub") != std::string::npos ||
         info.find("EPUB") != std::string::npos || info.find("PDF") != std::string::npos)) {
      if (resultCount_ < capacity_) {
        card_.format = (info.find("pdf") != std::string::npos || info.find("PDF") != std::string::npos) ? "pdf" : "epub";
        results_[resultCount_++] = std::move(card_);
      }
    }
    card_ = ShadowLibraryBook{};
    info_.clear();
    capture_ = Capture::NONE;
    authorLinkCount_ = 0;
    emittedCard_ = false;
  }

  void finalizeTitleLink() {
    decodeBasicEntities(card_.title);
    if (resultCount_ < capacity_ && !card_.title.empty() && !card_.detailUrl.empty()) {
      card_.format = "epub";
      results_[resultCount_] = card_;
      pendingResultIndex_ = static_cast<int>(resultCount_++);
      emittedCard_ = true;
    }
  }

  void processTag() {
    const bool closing = tag_.starts_with("</");
    const std::string name = tagName(tag_);
    if (name.empty()) return;

    if (!closing && name == "div" && hasClass(tag_, "pt-3") && hasClass(tag_, "pb-3") &&
        hasClass(tag_, "border-b")) {
      if (inCard_) return;
      inCard_ = true;
      divDepth_ = 1;
      card_ = ShadowLibraryBook{};
      info_.clear();
      authorLinkCount_ = 0;
      capture_ = Capture::NONE;
      return;
    }

    if (name == "a") {
      if (closing) {
        if (capture_ == Capture::TITLE && titleLinkOpen_) {
          finalizeTitleLink();
          titleLinkOpen_ = false;
        } else if (capture_ == Capture::AUTHOR && pendingResultIndex_ >= 0 &&
                   pendingResultIndex_ < static_cast<int>(resultCount_)) {
          results_[pendingResultIndex_].author = card_.author;
        }
        if (capture_ == Capture::TITLE || capture_ == Capture::AUTHOR || capture_ == Capture::PUBLISHER) {
          capture_ = Capture::NONE;
        }
        return;
      }

      const std::string href = attribute(tag_, "href");
      if (hasClass(tag_, "js-vim-focus") && href.starts_with("/md5/")) {
        card_.title.clear();
        card_.author.clear();
        card_.detailUrl = absoluteUrl(href);
        titleLinkOpen_ = true;
        capture_ = Capture::TITLE;
      } else if (inCard_ && href.starts_with("/search?q=") && !card_.detailUrl.empty()) {
        capture_ = authorLinkCount_++ == 0 ? Capture::AUTHOR : Capture::PUBLISHER;
      }
      return;
    }

    if (!inCard_) return;

    if (!closing && name == "div" && hasClass(tag_, "text-gray-800")) {
      capture_ = Capture::INFO;
    }

    if (name == "div") {
      if (closing) {
        if (divDepth_ > 0) --divDepth_;
        if (divDepth_ == 0) {
          finalizeCard();
          inCard_ = false;
        }
      } else {
        ++divDepth_;
      }
      if (closing && capture_ == Capture::INFO) capture_ = Capture::NONE;
      return;
    }

  }

  ShadowLibraryBook* results_;
  size_t capacity_;
  size_t resultCount_ = 0;
  bool inTag_ = false;
  bool inCard_ = false;
  int divDepth_ = 0;
  unsigned authorLinkCount_ = 0;
  Capture capture_ = Capture::NONE;
  std::string tag_;
  std::string info_;
  ShadowLibraryBook card_;
  int pendingResultIndex_ = -1;
  bool titleLinkOpen_ = false;
  bool emittedCard_ = false;

 public:
  size_t resultCount() const { return resultCount_; }
};

class MirrorParser {
 public:
  bool write(const uint8_t* data, const size_t length) {
    for (size_t i = 0; i < length; ++i) {
      const char c = static_cast<char>(data[i]);
      if (inTag_) {
        if (tag_.size() < MAX_TAG_BYTES) tag_.push_back(c);
        if (c == '>') {
          processTag();
          tag_.clear();
          inTag_ = false;
        }
      } else if (c == '<') {
        inTag_ = true;
        tag_.clear();
        tag_.push_back(c);
      }
    }
    return true;
  }

  const std::string& slowUrl() const { return slowUrl_; }
  const std::string& directUrl() const { return directUrl_; }

 private:
  void processTag() {
    if (tag_.starts_with("</") || tagName(tag_) != "a") return;
    const std::string href = attribute(tag_, "href");
    if (href.empty()) return;
    if (href.find("/slow_download/") != std::string::npos && slowUrl_.empty()) {
      slowUrl_ = absoluteUrl(href);
    }
    if (directUrl_.empty() && href.starts_with("http") && href.find("annas-archive.se") == std::string::npos &&
        (href.find("ipfs") != std::string::npos || href.find("download") != std::string::npos ||
         href.find(".epub") != std::string::npos || href.find(".pdf") != std::string::npos)) {
      directUrl_ = href;
    }
  }

  bool inTag_ = false;
  std::string tag_;
  std::string slowUrl_;
  std::string directUrl_;
};
}  // namespace

bool ShadowLibraryClient::fetchSearchPage(const std::string& url, ShadowLibraryBook* results, const size_t capacity,
                                          size_t& resultCount) {
  SearchParser parser(results, capacity);
  if (!HttpDownloader::fetchUrl(url, [&parser](const uint8_t* data, size_t len) { return parser.write(data, len); })) {
    return false;
  }
  parser.finish();
  resultCount = parser.resultCount();
  LOG_DBG("SHADOW", "Search parser produced %zu results", resultCount);
  return true;
}

bool ShadowLibraryClient::search(const std::string& query, ShadowLibraryBook* results, const size_t capacity,
                                 size_t& resultCount) {
  resultCount = 0;
  if (query.empty() || results == nullptr || capacity == 0) return false;
  const std::string url = std::string(BASE_URL) + "/search?q=" + encodeQuery(query);
  LOG_DBG("SHADOW", "Searching: %s", url.c_str());
  std::string body;
  if (!HttpDownloader::fetchUrl(url, body)) {
    LOG_ERR("SHADOW", "Relay search request failed");
    return false;
  }
  DynamicJsonDocument doc(8192);
  const DeserializationError error = deserializeJson(doc, body.c_str());
  if (error) {
    LOG_ERR("SHADOW", "Relay JSON parse failed: %s", error.c_str());
    return false;
  }
  JsonArray books = doc["results"].as<JsonArray>();
  if (books.isNull()) return false;
  for (JsonObject book : books) {
    if (resultCount >= capacity) break;
    results[resultCount].title = book["title"] | "";
    results[resultCount].author = book["author"] | "";
    results[resultCount].size = book["size"] | "";
    results[resultCount].downloads = book["downloads"] | "";
    results[resultCount].format = book["format"] | "epub";
    results[resultCount].detailUrl = book["download"] | "";
    if (!results[resultCount].title.empty() && !results[resultCount].detailUrl.empty()) ++resultCount;
  }
  LOG_DBG("SHADOW", "Relay returned %zu results", resultCount);
  return resultCount > 0;
}

bool ShadowLibraryClient::fetchMirrorPage(const std::string& url, std::string& directUrl) {
  MirrorParser parser;
  if (!HttpDownloader::fetchUrl(url, [&parser](const uint8_t* data, size_t len) { return parser.write(data, len); })) {
    return false;
  }
  directUrl = parser.directUrl();
  return !directUrl.empty();
}

bool ShadowLibraryClient::resolveDownloadUrl(const ShadowLibraryBook& book, std::string& downloadUrl) {
  downloadUrl.clear();
  if (book.detailUrl.empty()) return false;
  // The Worker resolves mirrors and proxies the file, so the ESP32 only needs
  // one trusted HTTPS connection and never contacts the unstable source domain.
  downloadUrl = book.detailUrl;
  return true;
}
