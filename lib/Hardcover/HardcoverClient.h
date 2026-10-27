#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct HardcoverLibraryBook {
  int bookId = 0;
  int statusId = 0;
  float rating = 0;
  int progressPages = 0;
  int pages = 0;
  int editionId = 0;
  int localProgressPercent = -1;
  bool hasPendingUpdate = false;
  std::string title;
  std::string author;
};

struct HardcoverBookSearchResult {
  int bookId = 0;
  int editionId = 0;
  int pages = 0;
  std::string author;
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
  static Error updateProgress(int bookId, int progressPercent, int editionId = 0, int editionPages = 0);
  static Error rateBook(int bookId, float rating, int statusId = 0);
  static Error fetchLibrary(std::vector<HardcoverLibraryBook>& outBooks, int limit = 20, int bookId = 0,
                            int offset = 0);
  static Error getBookDetails(int bookId, HardcoverBookSearchResult& outBook);
  static Error searchBook(const std::string& query, HardcoverBookSearchResult& outBook);
  static Error searchBook(const std::string& title, const std::string& author, HardcoverBookSearchResult& outBook);
  static Error searchBooks(const std::string& title, const std::string& author,
                           std::vector<HardcoverBookSearchResult>& outBooks, int limit = 5);
  // Release Wi-Fi resources retained by a completed Hardcover session.
  static void shutdownNetwork();
  static const char* errorString(Error error);
  static int lastHttpCode;
  static int lastTransportError;
  static const char* lastErrorDetail();
};
