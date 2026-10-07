#include "HardcoverSettingsActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>

#include "HardcoverClient.h"
#include "HardcoverCredentialStore.h"
#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
const StrId kMenuItems[3] = {StrId::STR_HARDCOVER_API_KEY, StrId::STR_AUTHENTICATE, StrId::STR_CLEAR};

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
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    handleSelection();
    return;
  }
  buttonNavigator.onNext([this] {
    selectedIndex = (selectedIndex + 1) % Count;
    requestUpdate();
  });
  buttonNavigator.onPrevious([this] {
    selectedIndex = (selectedIndex + Count - 1) % Count;
    requestUpdate();
  });
}

void HardcoverSettingsActivity::handleSelection() {
  if (selectedIndex == ImportKey) {
    const bool imported = HARDCOVER_STORE.importTokenFile();
    GUI.drawPopup(renderer, imported ? tr(STR_HARDCOVER_TOKEN_IMPORTED) : tr(STR_HARDCOVER_TOKEN_MISSING));
    requestUpdate();
  } else if (selectedIndex == Authenticate) {
    if (!HARDCOVER_STORE.hasApiToken()) {
      GUI.drawPopup(renderer, tr(STR_HARDCOVER_TOKEN_MISSING));
      requestUpdate();
      return;
    }
    if (!HARDCOVER_STORE.saveToFile()) {
      LOG_ERR("HDC", "Could not persist API token before minimal authentication boot");
      GUI.drawPopup(renderer, tr(STR_HARDCOVER_AUTH_START_FAILED));
      requestUpdate();
      return;
    }
    silentRestartToNetwork(NetworkBootTarget::HARDCOVER_AUTH);
  } else if (selectedIndex == ClearKey) {
    HARDCOVER_STORE.clearApiToken();
    requestUpdate();
  }
}

void HardcoverSettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_HARDCOVER));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentBottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
  int listTop = contentTop;
  if (!HARDCOVER_STORE.hasApiToken()) {
    const int textX = metrics.contentSidePadding;
    const int textWidth = pageWidth - metrics.contentSidePadding * 2;
    int y = contentTop + 8;
    y = drawWrappedLineBlock(renderer, UI_10_FONT_ID, textX, y, textWidth, tr(STR_HARDCOVER_SETUP_HINT));
    y = drawWrappedLineBlock(renderer, UI_10_FONT_ID, textX, y + 4, textWidth, tr(STR_HARDCOVER_SETUP_HINT_2));
    y = drawWrappedLineBlock(renderer, UI_10_FONT_ID, textX, y + 4, textWidth, tr(STR_HARDCOVER_SETUP_HINT_3));
    listTop = y + metrics.verticalSpacing;
  }
  GUI.drawList(
      renderer, Rect{0, listTop, pageWidth, std::max(0, contentBottom - listTop)}, Count, selectedIndex,
      [](int index) { return std::string(I18N.get(kMenuItems[index])); }, nullptr, nullptr,
      [](int index) {
        if (index == ImportKey)
          return HARDCOVER_STORE.hasApiToken() ? std::string("******") : std::string(tr(STR_NOT_SET));
        if (index == Authenticate) return HARDCOVER_STORE.getUsername();
        return std::string("");
      },
      true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
