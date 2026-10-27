#include "HardcoverSettingsActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>

#include "HardcoverClient.h"
#include "HardcoverCredentialStore.h"
#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
const StrId kMenuItems[3] = {StrId::STR_HARDCOVER_API_KEY, StrId::STR_HARDCOVER_CHECK_ACCOUNT,
                             StrId::STR_HARDCOVER_REMOVE_ACCOUNT};

int drawWrappedLineBlock(const GfxRenderer& renderer, const int fontId, const int x, int y, const int maxWidth,
                         const char* text, const int maxLines = 2) {
  const auto lines = renderer.wrappedText(fontId, text, maxWidth, maxLines);
  const int lineHeight = renderer.getLineHeight(fontId) + 4;
  for (const auto& line : lines) {
    renderer.drawText(fontId, x, y, line.c_str());
    y += lineHeight;
  }
  return y;
}

}  // namespace

void HardcoverSettingsActivity::onEnter() {
  Activity::onEnter();
  HARDCOVER_STORE.loadFromFile();
  requestUpdate();
}

void HardcoverSettingsActivity::onExit() {
  Activity::onExit();
  if (auto* const fontCache = renderer.getFontCacheManager()) {
    fontCache->clearCache();
  }
  if (!keepNetworkForParent) {
    HardcoverClient::shutdownNetwork();
  }
}

void HardcoverSettingsActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finishAfterBackPress();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    handleSelection();
    return;
  }
  buttonNavigator.onNext([this] {
    selectedIndex = (selectedIndex + 1) % (HARDCOVER_STORE.hasApiToken() ? Count : 1);
    requestUpdate();
  });
  buttonNavigator.onPrevious([this] {
    selectedIndex = (selectedIndex + Count - 1) % (HARDCOVER_STORE.hasApiToken() ? Count : 1);
    requestUpdate();
  });
}

void HardcoverSettingsActivity::handleSelection() {
  if (selectedIndex == ImportKey) {
    const bool imported = HARDCOVER_STORE.importTokenFile();
    feedback = imported ? StrId::STR_HARDCOVER_TOKEN_IMPORTED : StrId::STR_HARDCOVER_TOKEN_MISSING;
    requestUpdate();
  } else if (selectedIndex == Authenticate) {
    if (!HARDCOVER_STORE.hasApiToken()) {
      feedback = StrId::STR_HARDCOVER_TOKEN_MISSING;
      requestUpdate();
      return;
    }
    if (!HARDCOVER_STORE.saveToFile()) {
      LOG_ERR("HDC", "Could not persist API token before minimal authentication boot");
      feedback = StrId::STR_HARDCOVER_AUTH_START_FAILED;
      requestUpdate();
      return;
    }
    if (bookOrigin) {
      setResult(HardcoverResult{true});
      finish();
    } else
      silentRestartToNetwork(NetworkBootTarget::HARDCOVER_AUTH, keepNetworkForParent ? 2 : 0);
  } else if (selectedIndex == ClearKey) {
    auto confirm = makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_HARDCOVER_REMOVE_ACCOUNT),
                                                           tr(STR_HARDCOVER_REMOVE_CONFIRM));
    if (!confirm) {
      LOG_ERR("HDC", "OOM opening account removal confirmation");
      feedback = StrId::STR_MEMORY_ERROR;
      requestUpdate();
      return;
    }
    startActivityForResult(std::move(confirm), [this](const ActivityResult& result) {
      if (!result.isCancelled) {
        HARDCOVER_STORE.clearApiToken();
        feedback =
            HARDCOVER_STORE.hasApiToken() ? StrId::STR_HARDCOVER_QUEUE_FAILED : StrId::STR_HARDCOVER_ACCOUNT_REMOVED;
      }
      selectedIndex = 0;
      requestUpdate();
    });
  }
}

void HardcoverSettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const int pageWidth = safe.width;
  const int pageHeight = safe.y + safe.height;

  GUI.drawHeader(renderer, Rect{safe.x, safe.y + metrics.topPadding, pageWidth, metrics.headerHeight},
                 tr(STR_HARDCOVER));

  const int contentTop = safe.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentBottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
  int listTop = contentTop;
  const char* accountState = !HARDCOVER_STORE.hasApiToken()    ? tr(STR_HARDCOVER_NO_ACCOUNT)
                             : HARDCOVER_STORE.getUserId() > 0 ? HARDCOVER_STORE.getUsername().c_str()
                                                               : tr(STR_HARDCOVER_KEY_UNVERIFIED);
  GUI.drawSubHeader(renderer, Rect{safe.x, listTop, pageWidth, metrics.tabBarHeight}, accountState);
  listTop += metrics.tabBarHeight + metrics.verticalSpacing;
  if (feedback != StrId::STR_NONE_OPT) {
    GUI.drawSubHeader(renderer, Rect{safe.x, listTop, pageWidth, metrics.tabBarHeight}, I18N.get(feedback));
    listTop += metrics.tabBarHeight + metrics.verticalSpacing;
  }
  if (!HARDCOVER_STORE.hasApiToken()) {
    const int textX = safe.x + metrics.contentSidePadding;
    const int textWidth = pageWidth - metrics.contentSidePadding * 2;
    int y = listTop + 8;
    y = drawWrappedLineBlock(renderer, UI_10_FONT_ID, textX, y, textWidth, tr(STR_HARDCOVER_SETUP_HINT));
    y = drawWrappedLineBlock(renderer, UI_10_FONT_ID, textX, y + 4, textWidth, tr(STR_HARDCOVER_TOKEN_PATH));
    y = drawWrappedLineBlock(renderer, UI_10_FONT_ID, textX, y + 4, textWidth, tr(STR_HARDCOVER_SETUP_HINT_2));
    y = drawWrappedLineBlock(renderer, UI_10_FONT_ID, textX, y + 4, textWidth, tr(STR_HARDCOVER_SETUP_HINT_3));
    listTop = y + metrics.verticalSpacing;
  }
  GUI.drawList(
      renderer, Rect{safe.x, listTop, pageWidth, std::max(0, contentBottom - listTop)},
      HARDCOVER_STORE.hasApiToken() ? Count : 1, selectedIndex,
      [](int index) { return std::string(I18N.get(kMenuItems[index])); }, nullptr, nullptr,
      [](int index) {
        if (index == ImportKey)
          return HARDCOVER_STORE.hasApiToken() ? std::string("******") : std::string(tr(STR_NOT_SET));
        if (index == Authenticate) return HARDCOVER_STORE.getUsername();
        return std::string("");
      },
      true);

  const auto labels =
      mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), HARDCOVER_STORE.hasApiToken() ? tr(STR_DIR_UP) : "",
                            HARDCOVER_STORE.hasApiToken() ? tr(STR_DIR_DOWN) : "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
