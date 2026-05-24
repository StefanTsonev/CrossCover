#include "HardcoverLibraryActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <WiFi.h>

#include <cstdio>

#include "HardcoverCredentialStore.h"
#include "MappedInputManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/settings/HardcoverSettingsActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
const char* statusLabel(int statusId) {
  switch (statusId) {
    case 1:
      return "Want";
    case 2:
      return "Reading";
    case 3:
      return "Read";
    case 4:
      return "Paused";
    case 5:
      return "DNF";
  }
  return "";
}

const char* hardcoverErrorMessage(HardcoverClient::Error error, char* buffer, const size_t bufferSize) {
  if (!HardcoverClient::lastErrorDetail()[0]) {
    return HardcoverClient::errorString(error);
  }
  snprintf(buffer, bufferSize, "%s: %s", HardcoverClient::errorString(error), HardcoverClient::lastErrorDetail());
  return buffer;
}
}  // namespace

void HardcoverLibraryActivity::onEnter() {
  Activity::onEnter();
  HARDCOVER_STORE.loadFromFile();
  refresh();
}

void HardcoverLibraryActivity::refresh() {
  loaded = false;
  if (!HARDCOVER_STORE.hasApiToken()) {
    lastError = HardcoverClient::NO_TOKEN;
    requestUpdate();
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                           [this](const ActivityResult& result) {
                             if (!result.isCancelled) {
                               refresh();
                             } else {
                               lastError = HardcoverClient::NETWORK_ERROR;
                               requestUpdate();
                             }
                           });
    return;
  }
  lastError = HardcoverClient::fetchLibrary(books, 20);
  loaded = lastError == HardcoverClient::OK;
  requestUpdate();
}

void HardcoverLibraryActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (!HARDCOVER_STORE.hasApiToken()) {
      startActivityForResult(std::make_unique<HardcoverSettingsActivity>(renderer, mappedInput),
                             [this](const ActivityResult&) { refresh(); });
    } else {
      refresh();
    }
    return;
  }
  const int count = static_cast<int>(books.size());
  if (count > 0) {
    buttonNavigator.onNext([this, count] {
      selectedIndex = ButtonNavigator::nextIndex(selectedIndex, count);
      requestUpdate();
    });
    buttonNavigator.onPrevious([this, count] {
      selectedIndex = ButtonNavigator::previousIndex(selectedIndex, count);
      requestUpdate();
    });
  }
}

void HardcoverLibraryActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_HARDCOVER_LIBRARY));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;

  if (!loaded) {
    if (lastError == HardcoverClient::NO_TOKEN) {
      const int textX = metrics.contentSidePadding;
      int y = contentTop + 50;
      renderer.drawText(UI_10_FONT_ID, textX, y, tr(STR_HARDCOVER_SETUP_HINT));
      y += 32;
      renderer.drawText(UI_10_FONT_ID, textX, y, tr(STR_HARDCOVER_SETUP_HINT_2));
      y += 32;
      renderer.drawText(UI_10_FONT_ID, textX, y, tr(STR_HARDCOVER_SETUP_HINT_3));
    } else {
      char errorBuffer[128];
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2,
                                hardcoverErrorMessage(lastError, errorBuffer, sizeof(errorBuffer)));
    }
  } else if (books.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_HARDCOVER_NO_BOOKS));
  } else {
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(books.size()), selectedIndex,
        [this](int index) { return books[index].title; }, nullptr, nullptr,
        [this](int index) {
          const auto& book = books[index];
          char value[48];
          if (book.progressPages > 0 && book.pages > 0) {
            snprintf(value, sizeof(value), "%s %d/%d", statusLabel(book.statusId), book.progressPages, book.pages);
          } else if (book.rating > 0) {
            snprintf(value, sizeof(value), "%s %d*", statusLabel(book.statusId), book.rating);
          } else {
            snprintf(value, sizeof(value), "%s", statusLabel(book.statusId));
          }
          return std::string(value);
        },
        true);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_RETRY), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
