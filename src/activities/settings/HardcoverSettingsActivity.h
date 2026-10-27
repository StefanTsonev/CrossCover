#pragma once
#include <I18n.h>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class HardcoverSettingsActivity final : public Activity {
 public:
  explicit HardcoverSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                     bool keepNetworkForParent = false, bool bookOrigin = false)
      : Activity("HardcoverSettings", renderer, mappedInput),
        keepNetworkForParent(keepNetworkForParent),
        bookOrigin(bookOrigin) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum Item : int { ImportKey = 0, Authenticate = 1, ClearKey = 2, Count = 3 };
  int selectedIndex = 0;
  const bool keepNetworkForParent;
  const bool bookOrigin;
  StrId feedback = StrId::STR_NONE_OPT;
  ButtonNavigator buttonNavigator;

  void handleSelection();
};
