#include "HardcoverSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <WiFi.h>

#include <algorithm>
#include <cstdio>
#include <iterator>

#include "HardcoverClient.h"
#include "HardcoverCredentialStore.h"
#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
const StrId kMenuItems[4] = {StrId::STR_HARDCOVER_API_KEY, StrId::STR_AUTHENTICATE,
                             StrId::STR_HARDCOVER_AUTO_SYNC_THRESHOLD, StrId::STR_CLEAR};
constexpr uint8_t kAutoSyncThresholds[] = {1, 5, 10, 15};

const char* hardcoverErrorMessage(HardcoverClient::Error error, char* buffer, const size_t bufferSize) {
  if (!HardcoverClient::lastErrorDetail()[0]) {
    return HardcoverClient::errorString(error);
  }
  snprintf(buffer, bufferSize, "%s: %s", HardcoverClient::errorString(error), HardcoverClient::lastErrorDetail());
  return buffer;
}

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

uint8_t nextAutoSyncThreshold(uint8_t value) {
  value = CrossPointSettings::normalizeHardcoverAutoSyncThreshold(value);
  for (size_t i = 0; i < std::size(kAutoSyncThresholds); i++) {
    if (kAutoSyncThresholds[i] == value) {
      return kAutoSyncThresholds[(i + 1) % std::size(kAutoSyncThresholds)];
    }
  }
  return kAutoSyncThresholds[0];
}
}  // namespace

void HardcoverSettingsActivity::onEnter() {
  Activity::onEnter();
  HARDCOVER_STORE.loadFromFile();
  SETTINGS.hardcoverAutoSyncThresholdPercent =
      CrossPointSettings::normalizeHardcoverAutoSyncThreshold(SETTINGS.hardcoverAutoSyncThresholdPercent);
  requestUpdate();
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
    if (WiFi.status() != WL_CONNECTED) {
      startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                             [this](const ActivityResult& result) {
                               if (!result.isCancelled) {
                                 handleSelection();
                               } else {
                                 GUI.drawPopup(renderer, tr(STR_WIFI_CONN_FAILED));
                                 requestUpdate();
                               }
                             });
      return;
    }

    const auto error = HardcoverClient::authenticate();
    char errorBuffer[128];
    GUI.drawPopup(renderer,
                  error == HardcoverClient::OK ? tr(STR_HARDCOVER_AUTH_READY)
                                               : hardcoverErrorMessage(error, errorBuffer, sizeof(errorBuffer)));
    requestUpdate();
  } else if (selectedIndex == AutoSyncThreshold) {
    SETTINGS.hardcoverAutoSyncThresholdPercent =
        nextAutoSyncThreshold(SETTINGS.hardcoverAutoSyncThresholdPercent);
    SETTINGS.saveToFile();
    requestUpdate();
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
      renderer, Rect{0, listTop, pageWidth, std::max(0, contentBottom - listTop)},
      Count, selectedIndex,
      [](int index) { return std::string(I18N.get(kMenuItems[index])); }, nullptr, nullptr,
      [](int index) {
        if (index == ImportKey) return HARDCOVER_STORE.hasApiToken() ? std::string("******") : std::string(tr(STR_NOT_SET));
        if (index == Authenticate) return HARDCOVER_STORE.getUsername();
        if (index == AutoSyncThreshold) return std::to_string(SETTINGS.hardcoverAutoSyncThresholdPercent) + "%";
        return std::string("");
      },
      true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
