#pragma once

#include <I18n.h>

#include "activities/Activity.h"

class DictionaryActivity final : public Activity {
 public:
  DictionaryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string selectedWord);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }
  bool allowPowerAsConfirmInReaderMode() const override { return true; }

 private:
  std::string selectedWord;
  std::string definition;
  bool lookupComplete = false;
  StrId status = StrId::STR_DICTIONARY_NOT_FOUND;
};
