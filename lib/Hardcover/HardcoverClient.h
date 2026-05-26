#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct HardcoverLibraryBook {
  int bookId = 0;
  int statusId = 0;
  int rating = 0;
  int progressPages = 0;
  int pages = 0;
  std::string title;
  std::string author;
};

struct HardcoverBookSearchResult {
  int bookId = 0;
  std::string title;
};

class HardcoverClient {
 public:
  enum Error : uint8_t {
    OK = 0,
    NO_TOKEN,
    LOW_MEMORY,
    NETWORK_ERROR,
    AUTH_FAILED,
    RATE_LIMITED,
    SERVER_ERROR,
    JSON_ERROR,
    API_ERROR,
  };

  static Error authenticate();
  static Error upsertBookStatus(int bookId, int statusId);
  static Error updateProgress(int bookId, int progressPercent);
  static Error rateBook(int bookId, int rating);
  static Error fetchLibrary(std::vector<HardcoverLibraryBook>& outBooks, int limit = 20);
  static Error searchBook(const std::string& query, HardcoverBookSearchResult& outBook);
  static Error searchBook(const std::string& title, const std::string& author, HardcoverBookSearchResult& outBook);
  static Error searchBooks(const std::string& title, const std::string& author,
                           std::vector<HardcoverBookSearchResult>& outBooks, int limit = 5);
  static const char* errorString(Error error);
  static int lastHttpCode;
  static int lastTransportError;
  static const char* lastErrorDetail();
};
