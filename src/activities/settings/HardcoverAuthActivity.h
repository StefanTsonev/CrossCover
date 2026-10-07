#pragma once

#include <cstdint>
#include <string>

#include "activities/Activity.h"
#include "activities/ScreenTransitionRefresh.h"

class HardcoverAuthActivity final : public Activity {
 public:
  HardcoverAuthActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("HardcoverAuth", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state == AUTHENTICATING; }

 private:
  enum State : uint8_t { CONNECTING, AUTHENTICATING, SUCCESS, FAILED };

  State state = CONNECTING;
  ScreenTransitionRefresh screenTransitionRefresh;
  std::string statusMessage;
  std::string errorMessage;

  void onWifiSelectionComplete(bool connected);
  void performAuthentication();
};
