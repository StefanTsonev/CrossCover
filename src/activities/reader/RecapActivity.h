#pragma once
#include <I18n.h>

#include <atomic>
#include <string>
#include <vector>

#include "activities/Activity.h"
class RecapActivity final : public Activity {
 public:
  RecapActivity(GfxRenderer& renderer, MappedInputManager& input, bool network = false)
      : Activity("Recap", renderer, input), network(network) {}
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  bool network;
  bool busy = false;
  bool retryable = false;
  uint32_t quotaWaitSeconds = 0;
  std::atomic<bool> uploadStarted{false};
  bool preventAutoSleep() override { return busy; }
  int page = 0;
  int pages = 1;
  std::atomic<size_t> uploadDone{0};
  std::atomic<size_t> uploadTotal{0};
  uint32_t lastUploadRefresh = 0;
  unsigned lastUploadPercent = 101;
  std::string summary;
  std::string scope;
  std::vector<std::string> lines;
  int lineWidth = 0;
  StrId message = StrId::STR_RECAP_CONFIRM;
};
