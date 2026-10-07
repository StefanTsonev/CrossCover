#pragma once

#include <memory>

#include "activities/Activity.h"
#include "network/ShadowLibraryClient.h"
#include "util/ButtonNavigator.h"

/** Search and download books from the LibGen HTML catalog through the relay. */
class ShadowLibraryActivity final : public Activity {
 public:
  enum class State { CHECK_WIFI, WIFI_SELECTION, SEARCH_INPUT, SEARCHING, BROWSING, DOWNLOADING, ERROR };

  explicit ShadowLibraryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ShadowLibrary", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  static constexpr size_t DOWNLOAD_BUFFER_SIZE = 2048;
  static constexpr int PAGE_ITEMS = 3;
  static constexpr int COVER_WIDTH = 123;
  static constexpr int COVER_HEIGHT = 180;
  static constexpr const char* COVER_CACHE_DIR = "/.crosspoint/libgen_covers";

  State state = State::CHECK_WIFI;
  std::unique_ptr<ShadowLibraryBook[]> results;
  size_t resultCount = 0;
  int selectedIndex = 0;
  bool consumeConfirm = false;
  std::string statusMessage;
  std::string errorMessage;
  size_t downloadProgress = 0;
  size_t downloadTotal = 0;
  ButtonNavigator buttonNavigator;

  void checkAndConnectWifi();
  void launchWifiSelection();
  void launchSearch();
  void performSearch(const std::string& query);
  void prepareCoverThumbnails(size_t firstResult, size_t count);
  void loadVisibleCoverThumbnails();
  int itemsPerPage() const;
  void renderResultCard(int index, int x, int y, int width, int height, bool selected) const;
  void downloadBook(const ShadowLibraryBook& book);
  bool preventAutoSleep() override;
};
