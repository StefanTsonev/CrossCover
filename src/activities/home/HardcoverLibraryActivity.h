#pragma once

#include <vector>

#include "HardcoverClient.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class HardcoverLibraryActivity final : public Activity {
 public:
  explicit HardcoverLibraryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("HardcoverLibrary", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  std::vector<HardcoverLibraryBook> books;
  int selectedIndex = 0;
  bool loaded = false;
  HardcoverClient::Error lastError = HardcoverClient::OK;
  ButtonNavigator buttonNavigator;

  void refresh();
};
