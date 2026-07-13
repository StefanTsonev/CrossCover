#pragma once

#include <string>
#include <utility>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class HardcoverBookActivity final : public Activity {
 public:
  enum Action : int {
    LinkBook = 0,
    AutoLink = 1,
    MarkReading = 2,
    UpdateProgress = 3,
    MarkRead = 4,
    Rate = 5,
    Count = 6
  };

  explicit HardcoverBookActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string epubPath,
                                 std::string title, std::string author, int progressPercent)
      : Activity("HardcoverBook", renderer, mappedInput),
        epubPath(std::move(epubPath)),
        title(std::move(title)),
        author(std::move(author)),
        progressPercent(progressPercent) {}

  void onEnter() override;
  void onExit() override;
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
  int selectedIndex = 0;
  ButtonNavigator buttonNavigator;

  void handleSelection();
  void handleLinkInput(const std::string& input);
  void runAutoLink();
  void queueAction(Action action, int rating = 0);
};
