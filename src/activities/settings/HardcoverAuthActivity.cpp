#include "HardcoverAuthActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <cstdio>

#include "HardcoverClient.h"
#include "HardcoverCredentialStore.h"
#include "MappedInputManager.h"
#include "SdCardFontSystem.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/TouchHeaderBackButton.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/WifiUtils.h"

void HardcoverAuthActivity::onEnter() {
  Activity::onEnter();
  HARDCOVER_STORE.loadFromFile();
  sdFontSystem.releaseForNetwork(renderer);

  if (!HARDCOVER_STORE.hasApiToken()) {
    LOG_ERR("HDC", "Minimal authentication boot has no API token");
    state = FAILED;
    errorMessage = tr(STR_HARDCOVER_TOKEN_MISSING);
    requestUpdate();
    return;
  }

  if (hasActiveStationWifiConnection()) {
    onWifiSelectionComplete(true);
    return;
  }

  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void HardcoverAuthActivity::onWifiSelectionComplete(const bool connected) {
  if (!connected) {
    RenderLock lock(*this);
    state = FAILED;
    errorMessage = tr(STR_WIFI_CONN_FAILED);
    lock.unlock();
    requestUpdate();
    return;
  }

  WiFi.setSleep(false);
  sdFontSystem.releaseForNetwork(renderer);
  {
    RenderLock lock(*this);
    state = AUTHENTICATING;
    statusMessage = tr(STR_AUTHENTICATING);
  }
  if (requestUpdateAndWait() != RequestUpdateResult::Rendered) {
    LOG_ERR("HDC", "Authentication screen could not be rendered before request");
    requestUpdate(true);
  }
  performAuthentication();
}

void HardcoverAuthActivity::performAuthentication() {
  const uint32_t freeBefore = ESP.getFreeHeap();
  const uint32_t maxAllocBefore = ESP.getMaxAllocHeap();
  LOG_INF("HDC", "Starting minimal-boot authentication free=%u maxAlloc=%u", static_cast<unsigned>(freeBefore),
          static_cast<unsigned>(maxAllocBefore));

  const HardcoverClient::Error result = HardcoverClient::authenticate();
  const uint32_t freeAfter = ESP.getFreeHeap();
  const uint32_t maxAllocAfter = ESP.getMaxAllocHeap();
  LOG_INF("HDC", "Authentication finished result=%u free=%u maxAlloc=%u", static_cast<unsigned>(result),
          static_cast<unsigned>(freeAfter), static_cast<unsigned>(maxAllocAfter));

  {
    RenderLock lock(*this);
    if (result == HardcoverClient::OK) {
      state = SUCCESS;
      statusMessage = tr(STR_HARDCOVER_AUTH_READY);
    } else {
      state = FAILED;
      const char* const detail = HardcoverClient::lastErrorDetail();
      if (detail && detail[0]) {
        char message[128];
        snprintf(message, sizeof(message), "%s: %s", HardcoverClient::errorString(result), detail);
        errorMessage = message;
      } else {
        errorMessage = HardcoverClient::errorString(result);
      }
    }
  }
  requestUpdate();
}

void HardcoverAuthActivity::onExit() {
  Activity::onExit();
  HardcoverClient::shutdownNetwork();
  silentRestart();
}

void HardcoverAuthActivity::loop() {
  if (state != SUCCESS && state != FAILED) return;

  const Rect header = TouchHeaderBackButton::headerRect(renderer, mappedInput);
  if (TouchHeaderBackButton::wasTapped(mappedInput, header) ||
      mappedInput.wasPressed(MappedInputManager::Button::Back) ||
      mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    finishAfterBackPress();
  }
}

void HardcoverAuthActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  const Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const Rect header{0, metrics.topPadding, width, TouchHeaderBackButton::height(metrics, mappedInput)};
  if ((state == SUCCESS || state == FAILED) && mappedInput.hasTouchHardware()) {
    TouchHeaderBackButton::draw(renderer, header, tr(STR_HARDCOVER), false);
  } else {
    GUI.drawHeader(renderer, header, tr(STR_HARDCOVER));
  }

  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int top = (height - lineHeight) / 2;
  if (state == CONNECTING || state == AUTHENTICATING) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, state == AUTHENTICATING ? statusMessage.c_str() : tr(STR_CONNECTING));
  } else if (state == SUCCESS) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, statusMessage.c_str(), true, EpdFontFamily::BOLD);
  } else {
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_AUTH_FAILED), true, EpdFontFamily::BOLD);
    const Rect textArea{screen.x + metrics.contentSidePadding, screen.y, screen.width - metrics.contentSidePadding * 2,
                        screen.height};
    UITheme::drawCenteredWrappedText(renderer, textArea, UI_10_FONT_ID, top + lineHeight + 10, errorMessage.c_str(), 3,
                                     true, EpdFontFamily::REGULAR, 4);
  }

  const auto labels = mappedInput.mapLabels(mappedInput.withBackArrow(tr(STR_BACK)), tr(STR_OK), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer(screenTransitionRefresh.modeFor(static_cast<uint8_t>(state)));
}
