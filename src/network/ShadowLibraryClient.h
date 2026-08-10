#pragma once

#include <cstddef>
#include <string>

constexpr size_t MAX_SHADOW_LIBRARY_RESULTS = 8;

struct ShadowLibraryBook {
  std::string title;
  std::string author;
  std::string size;
  std::string downloads;
  std::string detailUrl;
  std::string format;
};

/**
 * Small streaming client for the HTML search flow used by Anna's Archive.
 * It deliberately keeps only a handful of result records in RAM; pages and
 * download responses are never assembled in one large heap buffer.
 */
class ShadowLibraryClient final {
 public:
  // Set this to the deployed Worker URL. The Worker performs HTML scraping;
  // the device only receives a small JSON response and streams the file.
#ifndef SHADOW_LIBRARY_BASE_URL
#define SHADOW_LIBRARY_BASE_URL "https://crossink-shadow-library.crosscover-annas.workers.dev"
#endif
  static constexpr const char* BASE_URL = SHADOW_LIBRARY_BASE_URL;

  static bool search(const std::string& query, ShadowLibraryBook* results, size_t capacity, size_t& resultCount);
  static bool resolveDownloadUrl(const ShadowLibraryBook& book, std::string& downloadUrl);

 private:
  static bool fetchSearchPage(const std::string& url, ShadowLibraryBook* results, size_t capacity, size_t& resultCount);
  static bool fetchMirrorPage(const std::string& url, std::string& directUrl);
};
