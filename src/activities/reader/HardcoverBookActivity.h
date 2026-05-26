#pragma once

#include <string>
#include <utility>
#include <vector>

#include "activities/Activity.h"
#include "HardcoverClient.h"
#include "util/ButtonNavigator.h"

class HardcoverBookActivity final : public Activity {
 public:
  enum Action : int {
    LinkBook = 0,
    AutoLink = 1,
    AutoSync = 2,
    MarkReading = 3,
    UpdateProgress = 4,
    MarkRead = 5,
    Rate = 6,
    Count = 7
  };

  explicit HardcoverBookActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string epubPath,
                                 std::string title, std::string author, int progressPercent)
      : Activity("HardcoverBook", renderer, mappedInput),
        epubPath(std::move(epubPath)),
        title(std::move(title)),
        author(std::move(author)),
        progressPercent(progressPercent) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }
  bool allowPowerAsConfirmInReaderMode() const override { return true; }

 private:
  std::string epubPath;
  std::string title;
  std::string author;
  int progressPercent;
  int bookId = 0;
  int lastSyncedProgress = -1;
  bool autoSync = false;
  bool selectingSearchResult = false;
  int selectedIndex = 0;
  int selectedSearchIndex = 0;
  std::vector<HardcoverBookSearchResult> searchResults;
  ButtonNavigator buttonNavigator;

  void handleSelection();
  void handleLinkInput(const std::string& input);
  void runAutoLink();
  void confirmSearchResult();
  void runManualSync(Action action, int rating = 0);
};
