#pragma once

#include <vector>

#include "HardcoverBookLinkStore.h"
#include "HardcoverClient.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class HardcoverLibraryActivity final : public Activity {
 public:
  explicit HardcoverLibraryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("HardcoverLibrary", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  std::vector<HardcoverLibraryBook> books;
  std::vector<HardcoverBookLink> pending;
  std::vector<HardcoverBookSearchResult> lookupResults;
  int selectedIndex = 0;
  int pendingLookupIndex = -1;
  int selectedLookupIndex = 0;
  bool loaded = false;
  bool selectingLookup = false;
  HardcoverClient::Error lastError = HardcoverClient::OK;
  ButtonNavigator buttonNavigator;

  void refresh();
  void syncPending();
  void confirmLookup();
};
