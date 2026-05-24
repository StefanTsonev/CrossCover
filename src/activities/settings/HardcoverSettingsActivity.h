#pragma once

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class HardcoverSettingsActivity final : public Activity {
 public:
  explicit HardcoverSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("HardcoverSettings", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum Item : int { ImportKey = 0, Authenticate = 1, ClearKey = 2, Count = 3 };
  int selectedIndex = 0;
  ButtonNavigator buttonNavigator;

  void handleSelection();
};
