#pragma once

#include <utility>
#include <vector>

#include "HardcoverBookLinkStore.h"
#include "HardcoverClient.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class HardcoverLibraryActivity final : public Activity {
 public:
  enum class Mode : uint8_t { Browse, RestartForBookSync, BookSyncNetwork, RestartForBookAuth, LibraryNetwork };
  explicit HardcoverLibraryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath = "",
                                    Mode mode = Mode::Browse)
      : Activity("HardcoverLibrary", renderer, mappedInput),
        bookPath(std::move(bookPath)),
        mode(mode),
        syncRequested(!this->bookPath.empty()) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class Operation : uint8_t { Idle, Loading, Syncing };
  Operation operation = Operation::Loading;
  // One persistent result line; no popup backdrop allocation on the C3.
  char syncSummary[96] = {};
  // Preserve request details: a later request resets the client's global buffer.
  char errorDetail[96] = {};
  std::string bookPath;
  Mode mode;
  bool syncRequested = false;
  std::vector<HardcoverLibraryBook> books;
  std::vector<HardcoverBookLink> pending;
  std::vector<HardcoverBookSearchResult> lookupResults;
  int selectedIndex = 0;
  int pendingLookupIndex = -1;
  int selectedLookupIndex = 0;
  bool loaded = false;
  bool selectingLookup = false;
  bool reviewingLookup = false;
  bool syncResultVisible = false;
  int sentChanges = 0;
  int sentUpdates = 0;
  bool syncFailed = false;
  bool viewingDetails = false;
  int detailIndex = 0;
  int pageOffset = 0;
  bool hasNextPage = false;
  static constexpr int PAGE_SIZE = 10;
  HardcoverClient::Error lastError = HardcoverClient::OK;
  ButtonNavigator buttonNavigator;

  void refresh(bool showLoading = true);
  void syncPending();
  void confirmLookup();
  bool loadPending();
  void setError(HardcoverClient::Error error, const char* detail = "");
  int pendingChanges() const;
  void showSyncResult();
  void confirmMatch();
};
