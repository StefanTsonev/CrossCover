#include "HardcoverClient.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#ifdef SIMULATOR
#include <ArduinoJsonStringCompat.h>
#endif
#include <Logging.h>
#include <WiFi.h>
#ifdef SIMULATOR
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#else
#include <esp_crt_bundle.h>
#include <esp_err.h>
#include <esp_http_client.h>
#endif

#include <cctype>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "HardcoverCredentialStore.h"

int HardcoverClient::lastHttpCode = 0;
int HardcoverClient::lastTransportError = 0;

namespace {
constexpr char API_URL[] = "https://api.hardcover.app/v1/graphql";
constexpr uint32_t MIN_HEAP_FOR_TLS = 55000;
char lastErrorDetailBuffer[96] = "";

struct UserBookRecord {
  int id = 0;
  int editionId = 0;
  int pages = 0;
  int readId = 0;
};

void setLastErrorDetail(const char* detail) {
  snprintf(lastErrorDetailBuffer, sizeof(lastErrorDetailBuffer), "%s", detail ? detail : "");
}

void setLastErrorDetail(const char* prefix, int httpCode, int transportError) {
  snprintf(lastErrorDetailBuffer, sizeof(lastErrorDetailBuffer), "%s http=%d err=%d", prefix, httpCode,
           transportError);
}

void copyBodyPreview(const char* body, char* preview, const size_t previewSize) {
  if (!preview || previewSize == 0) return;
  size_t i = 0;
  if (body) {
    for (; i < previewSize - 1 && body[i] != '\0'; i++) {
      const char c = body[i];
      preview[i] = (c == '\r' || c == '\n' || c == '\t') ? ' ' : c;
    }
  }
  preview[i] = '\0';
}

bool appendGraphqlStringLiteral(char* out, size_t outSize, size_t& pos, const char* text) {
  if (pos >= outSize) return false;
  out[pos++] = '"';
  if (text) {
    for (size_t i = 0; text[i] != '\0'; i++) {
      const char c = text[i];
      const bool needsEscape = c == '"' || c == '\\';
      const size_t needed = needsEscape ? 2 : 1;
      if (pos + needed >= outSize) return false;
      if (needsEscape) out[pos++] = '\\';
      out[pos++] = c;
    }
  }
  if (pos >= outSize) return false;
  out[pos++] = '"';
  out[pos] = '\0';
  return true;
}

bool hasGraphqlErrors(const JsonDocument& doc) {
  JsonArrayConst errors = doc["errors"].as<JsonArrayConst>();
  return !errors.isNull() && errors.size() > 0;
}

void setGraphqlErrorDetail(const JsonDocument& doc, const char* fallback) {
  const char* message = doc["errors"][0]["message"] | nullptr;
  if (message && message[0] != '\0') {
    setLastErrorDetail(message);
    return;
  }
  setLastErrorDetail(fallback);
}

HardcoverClient::Error postGraphql(const char* query, String& responseBody);

HardcoverClient::Error parseAuth(const char* body) {
  JsonDocument doc;
  const DeserializationError jsonError = deserializeJson(doc, body ? body : "");
  if (jsonError) {
    char preview[80];
    copyBodyPreview(body, preview, sizeof(preview));
    LOG_ERR("HDC", "Auth JSON parse failed: %s preview=\"%s\"", jsonError.c_str(), preview);
    setLastErrorDetail("JSON parse failed");
    return HardcoverClient::JSON_ERROR;
  }
  if (hasGraphqlErrors(doc)) {
    char preview[80];
    copyBodyPreview(body, preview, sizeof(preview));
    LOG_ERR("HDC", "Auth GraphQL error preview=\"%s\"", preview);
    setGraphqlErrorDetail(doc, "GraphQL auth error");
    return HardcoverClient::API_ERROR;
  }
  JsonVariantConst meValue = doc["data"]["me"];
  JsonObjectConst me = meValue.is<JsonArrayConst>() ? meValue[0].as<JsonObjectConst>() : meValue.as<JsonObjectConst>();
  const int id = me["id"] | 0;
  const char* username = me["username"] | "";
  if (id <= 0) {
    LOG_ERR("HDC", "Auth response did not include me.id");
    setLastErrorDetail("Auth response missing user");
    return HardcoverClient::AUTH_FAILED;
  }
  HARDCOVER_STORE.setUserInfo(id, username);
  HARDCOVER_STORE.saveToFile();
  return HardcoverClient::OK;
}

HardcoverClient::Error parseOk(const char* body) {
  JsonDocument doc;
  const DeserializationError jsonError = deserializeJson(doc, body ? body : "");
  if (jsonError) {
    char preview[80];
    copyBodyPreview(body, preview, sizeof(preview));
    LOG_ERR("HDC", "Mutation JSON parse failed: %s preview=\"%s\"", jsonError.c_str(), preview);
    setLastErrorDetail("JSON parse failed");
    return HardcoverClient::JSON_ERROR;
  }
  if (hasGraphqlErrors(doc)) {
    char preview[80];
    copyBodyPreview(body, preview, sizeof(preview));
    LOG_ERR("HDC", "Mutation GraphQL error preview=\"%s\"", preview);
    setGraphqlErrorDetail(doc, "GraphQL mutation error");
    return HardcoverClient::API_ERROR;
  }
  return HardcoverClient::OK;
}

HardcoverClient::Error parseUserBookRecord(const char* body, UserBookRecord& outRecord) {
  outRecord = {};
  JsonDocument doc;
  const DeserializationError jsonError = deserializeJson(doc, body ? body : "");
  if (jsonError) {
    char preview[80];
    copyBodyPreview(body, preview, sizeof(preview));
    LOG_ERR("HDC", "User book lookup JSON parse failed: %s preview=\"%s\"", jsonError.c_str(), preview);
    setLastErrorDetail("JSON parse failed");
    return HardcoverClient::JSON_ERROR;
  }
  if (hasGraphqlErrors(doc)) {
    char preview[80];
    copyBodyPreview(body, preview, sizeof(preview));
    LOG_ERR("HDC", "User book lookup GraphQL error preview=\"%s\"", preview);
    setGraphqlErrorDetail(doc, "GraphQL user book lookup error");
    return HardcoverClient::API_ERROR;
  }

  JsonObject userBook = doc["data"]["user_books"][0];
  outRecord.id = userBook["id"] | 0;
  outRecord.editionId = userBook["edition_id"] | 0;
  outRecord.pages = userBook["book"]["pages"] | 0;
  JsonArrayConst reads = userBook["user_book_reads"].as<JsonArrayConst>();
  if (!reads.isNull() && reads.size() > 0) {
    outRecord.readId = reads[0]["id"] | 0;
  }
  return HardcoverClient::OK;
}

HardcoverClient::Error parseLibrary(const char* body, std::vector<HardcoverLibraryBook>& outBooks) {
  JsonDocument doc;
  const DeserializationError jsonError = deserializeJson(doc, body ? body : "");
  if (jsonError) {
    char preview[80];
    copyBodyPreview(body, preview, sizeof(preview));
    LOG_ERR("HDC", "Library JSON parse failed: %s preview=\"%s\"", jsonError.c_str(), preview);
    setLastErrorDetail("JSON parse failed");
    return HardcoverClient::JSON_ERROR;
  }
  if (hasGraphqlErrors(doc)) {
    char preview[80];
    copyBodyPreview(body, preview, sizeof(preview));
    LOG_ERR("HDC", "Library GraphQL error preview=\"%s\"", preview);
    setGraphqlErrorDetail(doc, "GraphQL library error");
    return HardcoverClient::API_ERROR;
  }

  JsonArray books = doc["data"]["user_books"].as<JsonArray>();
  outBooks.clear();
  outBooks.reserve(books.size());
  for (JsonObject item : books) {
    HardcoverLibraryBook book;
    book.statusId = item["status_id"] | 0;
    book.rating = item["rating"] | 0;
    JsonArray reads = item["user_book_reads"].as<JsonArray>();
    if (!reads.isNull() && reads.size() > 0) {
      book.progressPages = reads[0]["progress_pages"] | 0;
    }
    JsonObject bookObj = item["book"];
    book.bookId = bookObj["id"] | 0;
    book.title = bookObj["title"] | std::string("");
    book.pages = bookObj["pages"] | 0;
    outBooks.push_back(std::move(book));
  }
  return HardcoverClient::OK;
}

int parseBookId(JsonVariantConst value) {
  if (value.is<int>()) return value.as<int>();
  if (value.is<const char*>()) return std::atoi(value.as<const char*>());
  return 0;
}

bool appendSearchIds(char* out, size_t outSize, size_t& pos, JsonArrayConst ids, int& appendedCount) {
  if (pos >= outSize) return false;
  out[pos++] = '[';
  appendedCount = 0;
  for (JsonVariantConst idValue : ids) {
    const int id = parseBookId(idValue);
    if (id <= 0) continue;
    const int written = snprintf(out + pos, outSize - pos, "%s%d", appendedCount > 0 ? "," : "", id);
    if (written <= 0 || static_cast<size_t>(written) >= outSize - pos) return false;
    pos += static_cast<size_t>(written);
    appendedCount++;
  }
  if (pos >= outSize) return false;
  out[pos++] = ']';
  out[pos] = '\0';
  return appendedCount > 0;
}

bool containsIgnoreCase(const char* text, const char* needle) {
  if (!text || !needle || needle[0] == '\0') return false;
  const size_t needleLen = strlen(needle);
  for (size_t i = 0; text[i] != '\0'; i++) {
    size_t j = 0;
    while (j < needleLen && text[i + j] != '\0' &&
           std::tolower(static_cast<unsigned char>(text[i + j])) == std::tolower(static_cast<unsigned char>(needle[j]))) {
      j++;
    }
    if (j == needleLen) return true;
  }
  return false;
}

bool looksLikeSetTitle(const char* title, const bool compilation) {
  if (compilation) return true;
  return containsIgnoreCase(title, " box set") || containsIgnoreCase(title, "boxed set") ||
         containsIgnoreCase(title, "4 books") || containsIgnoreCase(title, " collection") ||
         containsIgnoreCase(title, " bundle");
}

bool normalizeTitleToken(const char c, char* out, size_t outSize, size_t& pos, bool& pendingSpace) {
  if (pos + 1 >= outSize) return false;
  const unsigned char uc = static_cast<unsigned char>(c);
  if (std::isalnum(uc)) {
    if (pendingSpace && pos > 0) {
      out[pos++] = ' ';
      if (pos + 1 >= outSize) return false;
    }
    out[pos++] = static_cast<char>(std::tolower(uc));
    pendingSpace = false;
  } else if (c == ':' || c == '-' || c == '(' || c == '[') {
    return false;
  } else if (pos > 0) {
    pendingSpace = true;
  }
  out[pos] = '\0';
  return true;
}

void normalizeTitle(const char* title, char* out, size_t outSize) {
  if (!out || outSize == 0) return;
  out[0] = '\0';
  if (!title) return;
  size_t pos = 0;
  bool pendingSpace = false;
  for (size_t i = 0; title[i] != '\0'; i++) {
    if (!normalizeTitleToken(title[i], out, outSize, pos, pendingSpace)) {
      break;
    }
  }
  while (pos > 0 && out[pos - 1] == ' ') {
    pos--;
  }
  out[pos] = '\0';
}

bool titlesMatchClosely(const char* expectedTitle, const char* candidateTitle) {
  if (!expectedTitle || expectedTitle[0] == '\0') return true;
  char expected[96];
  char candidate[96];
  normalizeTitle(expectedTitle, expected, sizeof(expected));
  normalizeTitle(candidateTitle, candidate, sizeof(candidate));
  if (expected[0] == '\0' || candidate[0] == '\0') return false;
  const size_t expectedLen = strlen(expected);
  const size_t candidateLen = strlen(candidate);
  const size_t prefixLen = std::min(expectedLen, candidateLen);
  if (prefixLen < 5) return false;
  return strncmp(expected, candidate, prefixLen) == 0 || strstr(candidate, expected) != nullptr ||
         strstr(expected, candidate) != nullptr;
}

HardcoverClient::Error parseBookSearch(const char* body, const char* expectedTitle,
                                        std::vector<HardcoverBookSearchResult>& outBooks, const int limit) {
  outBooks.clear();
  JsonDocument doc;
  JsonDocument filter;
  filter["data"]["search"]["ids"][0] = true;
  filter["errors"][0]["message"] = true;
  const DeserializationError jsonError =
      deserializeJson(doc, body ? body : "", DeserializationOption::Filter(filter));
  if (jsonError) {
    char preview[80];
    copyBodyPreview(body, preview, sizeof(preview));
    LOG_ERR("HDC", "Book search JSON parse failed: %s preview=\"%s\"", jsonError.c_str(), preview);
    if (jsonError == DeserializationError::NoMemory) {
      setLastErrorDetail("Search response too large");
      return HardcoverClient::LOW_MEMORY;
    }
    setLastErrorDetail("Invalid search response");
    return body && body[0] == '\0' ? HardcoverClient::SERVER_ERROR : HardcoverClient::JSON_ERROR;
  }
  if (hasGraphqlErrors(doc)) {
    char preview[80];
    copyBodyPreview(body, preview, sizeof(preview));
    LOG_ERR("HDC", "Book search GraphQL error preview=\"%s\"", preview);
    setGraphqlErrorDetail(doc, "GraphQL search error");
    return HardcoverClient::API_ERROR;
  }

  JsonArrayConst ids = doc["data"]["search"]["ids"].as<JsonArrayConst>();
  if (ids.isNull() || ids.size() == 0) {
    setLastErrorDetail("No matching book found");
    return HardcoverClient::API_ERROR;
  }

  char query[384];
  size_t pos = 0;
  const int prefixLen = snprintf(query, sizeof(query), "query { books(where: {id: {_in: ");
  if (prefixLen <= 0 || static_cast<size_t>(prefixLen) >= sizeof(query)) return HardcoverClient::LOW_MEMORY;
  pos = static_cast<size_t>(prefixLen);
  int idCount = 0;
  if (!appendSearchIds(query, sizeof(query), pos, ids, idCount)) return HardcoverClient::LOW_MEMORY;
  const int suffixLen = snprintf(query + pos, sizeof(query) - pos,
                                 "}}, limit: %d) { id title compilation } }", idCount);
  if (suffixLen <= 0 || static_cast<size_t>(suffixLen) >= sizeof(query) - pos) return HardcoverClient::LOW_MEMORY;

  String detailsBody;
  HardcoverClient::Error err = postGraphql(query, detailsBody);
  if (err != HardcoverClient::OK) return err;

  JsonDocument detailsDoc;
  JsonDocument detailsFilter;
  detailsFilter["data"]["books"][0]["id"] = true;
  detailsFilter["data"]["books"][0]["title"] = true;
  detailsFilter["data"]["books"][0]["compilation"] = true;
  detailsFilter["errors"][0]["message"] = true;
  const DeserializationError detailsJsonError =
      deserializeJson(detailsDoc, detailsBody.c_str(), DeserializationOption::Filter(detailsFilter));
  if (detailsJsonError) {
    setLastErrorDetail("Invalid book detail response");
    return HardcoverClient::JSON_ERROR;
  }
  if (hasGraphqlErrors(detailsDoc)) {
    setGraphqlErrorDetail(detailsDoc, "GraphQL book detail error");
    return HardcoverClient::API_ERROR;
  }

  JsonArrayConst books = detailsDoc["data"]["books"].as<JsonArrayConst>();
  for (JsonVariantConst idValue : ids) {
    const int id = parseBookId(idValue);
    for (JsonObjectConst book : books) {
      const int bookId = book["id"] | 0;
      const char* title = book["title"] | "";
      const bool compilation = book["compilation"] | false;
      if (bookId == id && !looksLikeSetTitle(title, compilation)) {
        HardcoverBookSearchResult result;
        result.bookId = id;
        result.title = title;
        if (titlesMatchClosely(expectedTitle, title)) {
          outBooks.insert(outBooks.begin(), result);
        } else {
          outBooks.push_back(result);
        }
        break;
      }
    }
    if (static_cast<int>(outBooks.size()) >= limit) break;
  }

  if (outBooks.empty()) {
    setLastErrorDetail("No matching book found");
    return HardcoverClient::API_ERROR;
  }
  return HardcoverClient::OK;
}

HardcoverClient::Error parseBookSearch(const char* body, const char* expectedTitle,
                                        HardcoverBookSearchResult& outBook) {
  std::vector<HardcoverBookSearchResult> books;
  const HardcoverClient::Error err = parseBookSearch(body, expectedTitle, books, 1);
  if (err != HardcoverClient::OK) return err;
  outBook = books[0];
  return HardcoverClient::OK;
}

String makeBody(const char* query) {
  JsonDocument doc;
  doc["query"] = query;
  String body;
  serializeJson(doc, body);
  return body;
}

#ifdef SIMULATOR
HardcoverClient::Error postGraphql(const char* query, String& responseBody) {
  HardcoverClient::lastHttpCode = 0;
  HardcoverClient::lastTransportError = 0;
  setLastErrorDetail("");
  if (!HARDCOVER_STORE.hasApiToken()) return HardcoverClient::NO_TOKEN;
  if (WiFi.status() != WL_CONNECTED) {
    setLastErrorDetail("WiFi not connected");
    return HardcoverClient::NETWORK_ERROR;
  }

  HTTPClient http;
  WiFiClientSecure secureClient;
  secureClient.setInsecure();
  if (!http.begin(secureClient, API_URL)) {
    setLastErrorDetail("HTTP begin failed");
    return HardcoverClient::NETWORK_ERROR;
  }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/json");
  http.addHeader("User-Agent", "CrossInk-X4-Hardcover");
  http.addHeader("Authorization", HARDCOVER_STORE.getApiToken().c_str());

  String body = makeBody(query);
  const int httpCode = http.POST(body);
  HardcoverClient::lastHttpCode = httpCode;
  HardcoverClient::lastTransportError = httpCode < 0 ? httpCode : 0;
  responseBody = http.getString();
  http.end();

  if (httpCode == 200) return HardcoverClient::OK;
  if (httpCode == 401 || httpCode == 403) {
    setLastErrorDetail("Token expired or invalid");
    return HardcoverClient::AUTH_FAILED;
  }
  if (httpCode == 429) {
    setLastErrorDetail("Rate limited");
    return HardcoverClient::RATE_LIMITED;
  }
  if (httpCode < 0) {
    setLastErrorDetail("HTTP request failed", httpCode, HardcoverClient::lastTransportError);
    return HardcoverClient::NETWORK_ERROR;
  }
  return HardcoverClient::SERVER_ERROR;
}
#else
struct ResponseBuffer {
  char* data = nullptr;
  int len = 0;
  int capacity = 0;
  bool overflow = false;
  ~ResponseBuffer() { free(data); }
  bool ensure(int size) {
    if (size <= capacity) return true;
    char* next = static_cast<char*>(realloc(data, size));
    if (!next) return false;
    data = next;
    capacity = size;
    return true;
  }
};

esp_err_t httpEventHandler(esp_http_client_event_t* evt) {
  auto* buf = static_cast<ResponseBuffer*>(evt->user_data);
  if (evt->event_id == HTTP_EVENT_ON_DATA && buf) {
    if (buf->ensure(buf->len + evt->data_len + 1)) {
      memcpy(buf->data + buf->len, evt->data, evt->data_len);
      buf->len += evt->data_len;
      buf->data[buf->len] = '\0';
    } else {
      buf->overflow = true;
    }
  }
  return ESP_OK;
}

HardcoverClient::Error postGraphql(const char* query, String& responseBody) {
  HardcoverClient::lastHttpCode = 0;
  HardcoverClient::lastTransportError = 0;
  setLastErrorDetail("");
  if (!HARDCOVER_STORE.hasApiToken()) return HardcoverClient::NO_TOKEN;
  if (WiFi.status() != WL_CONNECTED) {
    LOG_ERR("HDC", "WiFi is not connected before Hardcover request (status=%d)", static_cast<int>(WiFi.status()));
    setLastErrorDetail("WiFi not connected");
    return HardcoverClient::NETWORK_ERROR;
  }
  const uint32_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < MIN_HEAP_FOR_TLS) {
    LOG_ERR("HDC", "Insufficient heap for TLS: %u bytes free (need %u)", static_cast<unsigned>(freeHeap),
            static_cast<unsigned>(MIN_HEAP_FOR_TLS));
    return HardcoverClient::LOW_MEMORY;
  }

  ResponseBuffer response;
  esp_http_client_config_t config = {};
  config.url = API_URL;
  config.event_handler = httpEventHandler;
  config.user_data = &response;
  config.method = HTTP_METHOD_POST;
  config.timeout_ms = 15000;
  config.buffer_size = 2048;
  config.buffer_size_tx = 2048;
  config.crt_bundle_attach = esp_crt_bundle_attach;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    LOG_ERR("HDC", "esp_http_client_init failed");
    setLastErrorDetail("HTTP init failed");
    return HardcoverClient::NETWORK_ERROR;
  }

  String body = makeBody(query);
  if (esp_http_client_set_header(client, "Content-Type", "application/json") != ESP_OK ||
      esp_http_client_set_header(client, "Accept", "application/json") != ESP_OK ||
      esp_http_client_set_header(client, "User-Agent", "CrossInk-X4-Hardcover") != ESP_OK ||
      esp_http_client_set_header(client, "Authorization", HARDCOVER_STORE.getApiToken().c_str()) != ESP_OK ||
      esp_http_client_set_post_field(client, body.c_str(), body.length()) != ESP_OK) {
    LOG_ERR("HDC", "Failed to configure Hardcover HTTP request");
    esp_http_client_cleanup(client);
    setLastErrorDetail("HTTP setup failed");
    return HardcoverClient::NETWORK_ERROR;
  }

  LOG_INF("HDC", "POST Hardcover GraphQL bytes=%u heap=%u", static_cast<unsigned>(body.length()),
          static_cast<unsigned>(freeHeap));
  esp_err_t err = esp_http_client_perform(client);
  const int httpCode = esp_http_client_get_status_code(client);
  HardcoverClient::lastHttpCode = httpCode;
  HardcoverClient::lastTransportError = static_cast<int>(err);
  if (response.data) responseBody = response.data;
  esp_http_client_cleanup(client);

  LOG_INF("HDC", "Hardcover response http=%d err=%d(%s) bytes=%d", httpCode, static_cast<int>(err),
          esp_err_to_name(err), response.len);
  if (response.overflow) {
    setLastErrorDetail("API response too large");
    return HardcoverClient::LOW_MEMORY;
  }
  if (httpCode == 200 && response.len == 0) {
    setLastErrorDetail("Empty API response");
    return HardcoverClient::SERVER_ERROR;
  }
  if (httpCode == 200) return HardcoverClient::OK;
  if (httpCode == 401 || httpCode == 403) {
    setLastErrorDetail("Token expired or invalid");
    return HardcoverClient::AUTH_FAILED;
  }
  if (httpCode == 429) {
    setLastErrorDetail("Rate limited");
    return HardcoverClient::RATE_LIMITED;
  }
  if (err != ESP_OK) {
    setLastErrorDetail("Transport failed", httpCode, static_cast<int>(err));
    return HardcoverClient::NETWORK_ERROR;
  }
  setLastErrorDetail("HTTP server error", httpCode, 0);
  return HardcoverClient::SERVER_ERROR;
}
#endif
}  // namespace

HardcoverClient::Error HardcoverClient::authenticate() {
  String body;
  Error err = postGraphql("query { me { id username } }", body);
  return err == OK ? parseAuth(body.c_str()) : err;
}

HardcoverClient::Error findUserBookRecord(const int bookId, UserBookRecord& outRecord) {
  const int userId = HARDCOVER_STORE.getUserId();
  if (userId <= 0) {
    setLastErrorDetail("Authenticate Hardcover first");
    return HardcoverClient::AUTH_FAILED;
  }

  char query[320];
  snprintf(query, sizeof(query),
           "query { user_books(where: {user_id: {_eq: %d}, book_id: {_eq: %d}}, limit: 1) { id edition_id book { "
           "pages } user_book_reads(order_by: {started_at: desc}, limit: 1) { id } } }",
           userId,
           bookId);
  String body;
  const HardcoverClient::Error err = postGraphql(query, body);
  return err == HardcoverClient::OK ? parseUserBookRecord(body.c_str(), outRecord) : err;
}

HardcoverClient::Error HardcoverClient::upsertBookStatus(int bookId, int statusId) {
  UserBookRecord userBook;
  Error err = findUserBookRecord(bookId, userBook);
  if (err != OK) return err;

  char query[256];
  if (userBook.id > 0) {
    snprintf(query, sizeof(query),
             "mutation { update_user_book(id: %d, object: {status_id: %d}) { id } }",
             userBook.id, statusId);
  } else {
    snprintf(query, sizeof(query),
             "mutation { insert_user_book(object: {book_id: %d, status_id: %d}) { id } }", bookId,
             statusId);
  }
  String body;
  err = postGraphql(query, body);
  return err == OK ? parseOk(body.c_str()) : err;
}

HardcoverClient::Error HardcoverClient::updateProgress(int bookId, int progressPercent) {
  UserBookRecord userBook;
  Error err = findUserBookRecord(bookId, userBook);
  if (err != OK) return err;
  if (userBook.id <= 0) {
    err = upsertBookStatus(bookId, 2);
    if (err != OK) return err;
    err = findUserBookRecord(bookId, userBook);
    if (err != OK) return err;
  }
  if (userBook.id <= 0) {
    setLastErrorDetail("Book is not in library");
    return API_ERROR;
  }

  if (progressPercent < 0) progressPercent = 0;
  if (progressPercent > 100) progressPercent = 100;
  int progressPages = progressPercent;
  if (userBook.pages > 0) {
    progressPages = (userBook.pages * progressPercent + 50) / 100;
  }

  char query[320];
  if (userBook.readId > 0) {
    snprintf(query, sizeof(query),
             "mutation { update_user_book_read(id: %d, object: {progress_pages: %d}) { id } }",
             userBook.readId, progressPages);
  } else if (userBook.editionId > 0) {
    snprintf(query, sizeof(query),
             "mutation { insert_user_book_read(user_book_id: %d, user_book_read: {progress_pages: %d, edition_id: %d}) "
             "{ id } }",
             userBook.id, progressPages, userBook.editionId);
  } else {
    snprintf(query, sizeof(query),
             "mutation { insert_user_book_read(user_book_id: %d, user_book_read: {progress_pages: %d}) { id } }",
             userBook.id, progressPages);
  }
  String body;
  err = postGraphql(query, body);
  return err == OK ? parseOk(body.c_str()) : err;
}

HardcoverClient::Error HardcoverClient::rateBook(int bookId, int rating) {
  UserBookRecord userBook;
  Error err = findUserBookRecord(bookId, userBook);
  if (err != OK) return err;

  char query[256];
  if (userBook.id > 0) {
    snprintf(query, sizeof(query),
             "mutation { update_user_book(id: %d, object: {rating: %d}) { id } }",
             userBook.id, rating);
  } else {
    snprintf(query, sizeof(query),
             "mutation { insert_user_book(object: {book_id: %d, status_id: 3, rating: %d}) { id } }",
             bookId, rating);
  }
  String body;
  err = postGraphql(query, body);
  return err == OK ? parseOk(body.c_str()) : err;
}

HardcoverClient::Error HardcoverClient::fetchLibrary(std::vector<HardcoverLibraryBook>& outBooks, int limit) {
  const int userId = HARDCOVER_STORE.getUserId();
  if (userId <= 0) {
    setLastErrorDetail("Authenticate Hardcover first");
    return AUTH_FAILED;
  }

  char query[768];
  snprintf(query, sizeof(query),
           "query { user_books(where: {user_id: {_eq: %d}}, limit: %d, order_by: {date_added: desc}) { status_id "
           "rating user_book_reads(order_by: {started_at: desc}, limit: 1) { progress_pages } book { id title pages } "
           "} }",
           userId, limit);
  String body;
  Error err = postGraphql(query, body);
  return err == OK ? parseLibrary(body.c_str(), outBooks) : err;
}

HardcoverClient::Error HardcoverClient::searchBook(const std::string& searchQuery, HardcoverBookSearchResult& outBook) {
  if (searchQuery.empty()) {
    setLastErrorDetail("Missing search text");
    return API_ERROR;
  }

  char query[384];
  size_t pos = 0;
  const int prefixLen =
      snprintf(query, sizeof(query), "query { search(query: ");
  if (prefixLen <= 0 || static_cast<size_t>(prefixLen) >= sizeof(query)) return LOW_MEMORY;
  pos = static_cast<size_t>(prefixLen);
  if (!appendGraphqlStringLiteral(query, sizeof(query), pos, searchQuery.c_str())) return LOW_MEMORY;
  const int suffixLen = snprintf(query + pos, sizeof(query) - pos,
                                 ", query_type: \"Book\", per_page: 5, page: 1) { ids } }");
  if (suffixLen <= 0 || static_cast<size_t>(suffixLen) >= sizeof(query) - pos) return LOW_MEMORY;

  String body;
  Error err = postGraphql(query, body);
  return err == OK ? parseBookSearch(body.c_str(), "", outBook) : err;
}

HardcoverClient::Error HardcoverClient::searchBook(const std::string& title, const std::string& author,
                                                   HardcoverBookSearchResult& outBook) {
  std::vector<HardcoverBookSearchResult> books;
  const Error err = searchBooks(title, author, books, 1);
  if (err != OK) return err;
  outBook = books[0];
  return OK;
}

HardcoverClient::Error HardcoverClient::searchBooks(const std::string& title, const std::string& author,
                                                    std::vector<HardcoverBookSearchResult>& outBooks,
                                                    const int limit) {
  outBooks.clear();
  char searchText[192];
  snprintf(searchText, sizeof(searchText), "%s%s%s", title.c_str(), author.empty() ? "" : " ", author.c_str());
  if (title.empty()) {
    HardcoverBookSearchResult book;
    const Error err = searchBook(searchText, book);
    if (err == OK) outBooks.push_back(book);
    return err;
  }

  char query[384];
  size_t pos = 0;
  const int prefixLen = snprintf(query, sizeof(query), "query { search(query: ");
  if (prefixLen <= 0 || static_cast<size_t>(prefixLen) >= sizeof(query)) return LOW_MEMORY;
  pos = static_cast<size_t>(prefixLen);
  if (!appendGraphqlStringLiteral(query, sizeof(query), pos, searchText)) return LOW_MEMORY;
  const int suffixLen = snprintf(query + pos, sizeof(query) - pos,
                                 ", query_type: \"Book\", per_page: 5, page: 1) { ids } }");
  if (suffixLen <= 0 || static_cast<size_t>(suffixLen) >= sizeof(query) - pos) return LOW_MEMORY;

  String body;
  Error err = postGraphql(query, body);
  return err == OK ? parseBookSearch(body.c_str(), "", outBooks, limit) : err;
}

const char* HardcoverClient::errorString(Error error) {
  switch (error) {
    case OK:
      return "OK";
    case NO_TOKEN:
      return "No API token";
    case LOW_MEMORY:
      return "Low memory";
    case NETWORK_ERROR:
      return "Network error";
    case AUTH_FAILED:
      return "Auth failed";
    case RATE_LIMITED:
      return "Rate limited";
    case SERVER_ERROR:
      return "Server error";
    case JSON_ERROR:
      return "JSON error";
    case API_ERROR:
      return "API error";
  }
  return "Unknown error";
}

const char* HardcoverClient::lastErrorDetail() { return lastErrorDetailBuffer; }
