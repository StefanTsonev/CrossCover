#pragma once

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class HardcoverSettingsActivity final : public Activity {
 public:
  explicit HardcoverSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                     bool keepNetworkForParent = false)
      : Activity("HardcoverSettings", renderer, mappedInput), keepNetworkForParent(keepNetworkForParent) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum Item : int { ImportKey = 0, Authenticate = 1, ClearKey = 2, Count = 3 };
  int selectedIndex = 0;
  const bool keepNetworkForParent;
  ButtonNavigator buttonNavigator;

  void handleSelection();
};
