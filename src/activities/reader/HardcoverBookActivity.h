#pragma once

#include <I18n.h>

#include <string>
#include <utility>

#include "HardcoverBookLinkStore.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class HardcoverBookActivity final : public Activity {
 public:
  enum Action : int { Status, UpdateProgress, Rate, LinkBook, Sync, Setup, Pending, Count };

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
  HardcoverBookLink link;
  Action actions[Count] = {};
  int actionCount = 0;
  bool hasPendingUpdate = false;
  StrId feedback = StrId::STR_NONE_OPT;
  int selectedIndex = 0;
  ButtonNavigator buttonNavigator;

  void handleSelection();
  void reloadState();
  int effectiveStatus() const;
  float effectiveRating() const;
  bool progressUnchanged() const;
  void selectStatus();
  void selectLinkMethod();
  void handleLinkInput(const std::string& input);
  void showQueueResult(bool queued);
  void showMemoryError();
  void sendUpdates(bool authenticate = false);
  void reviewPending();
};
