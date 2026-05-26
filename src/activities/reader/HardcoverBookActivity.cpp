#include "HardcoverBookActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <WiFi.h>

#include <cstdio>
#include <cstdlib>

#include "HardcoverBookLinkStore.h"
#include "HardcoverClient.h"
#include "HardcoverCredentialStore.h"
#include "MappedInputManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/settings/HardcoverSettingsActivity.h"
#include "activities/util/IntervalSelectionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
const StrId kMenuItems[HardcoverBookActivity::Count] = {
    StrId::STR_HARDCOVER_LINK_BOOK,       StrId::STR_HARDCOVER_AUTO_LINK,       StrId::STR_HARDCOVER_AUTO_SYNC,
    StrId::STR_HARDCOVER_MARK_READING,   StrId::STR_HARDCOVER_UPDATE_PROGRESS, StrId::STR_HARDCOVER_MARK_READ,
    StrId::STR_HARDCOVER_RATE,
};

bool looksLikeIsbn(const std::string& text) {
  int digitCount = 0;
  for (const char c : text) {
    if (c >= '0' && c <= '9') {
      digitCount++;
    } else if (c != '-' && c != ' ') {
      return false;
    }
  }
  return digitCount == 10 || digitCount == 13;
}

bool isPlainNumber(const std::string& text) {
  if (text.empty()) return false;
  for (const char c : text) {
    if (c < '0' || c > '9') return false;
  }
  return true;
}

const char* hardcoverErrorMessage(HardcoverClient::Error error, char* buffer, const size_t bufferSize) {
  if (!HardcoverClient::lastErrorDetail()[0]) {
    return HardcoverClient::errorString(error);
  }
  snprintf(buffer, bufferSize, "%s: %s", HardcoverClient::errorString(error), HardcoverClient::lastErrorDetail());
  return buffer;
}
}

void HardcoverBookActivity::onEnter() {
  Activity::onEnter();
  HARDCOVER_STORE.loadFromFile();
  HardcoverBookLink link;
  if (HARDCOVER_LINKS.getLink(epubPath, link)) {
    bookId = link.bookId;
    autoSync = link.autoSync;
    lastSyncedProgress = link.lastSyncedProgress;
  }
  requestUpdate();
}

void HardcoverBookActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (selectingSearchResult) {
      selectingSearchResult = false;
      searchResults.clear();
      selectedSearchIndex = 0;
      requestUpdate();
      return;
    }
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (selectingSearchResult) {
      confirmSearchResult();
      return;
    }
    handleSelection();
    return;
  }
  buttonNavigator.onNext([this] {
    if (selectingSearchResult) {
      if (searchResults.empty()) return;
      selectedSearchIndex = (selectedSearchIndex + 1) % static_cast<int>(searchResults.size());
    } else {
      selectedIndex = (selectedIndex + 1) % Count;
    }
    requestUpdate();
  });
  buttonNavigator.onPrevious([this] {
    if (selectingSearchResult) {
      if (searchResults.empty()) return;
      selectedSearchIndex =
          (selectedSearchIndex + static_cast<int>(searchResults.size()) - 1) % static_cast<int>(searchResults.size());
    } else {
      selectedIndex = (selectedIndex + Count - 1) % Count;
    }
    requestUpdate();
  });
}

void HardcoverBookActivity::handleSelection() {
  const auto action = static_cast<Action>(selectedIndex);
  if (!HARDCOVER_STORE.hasApiToken() && action != LinkBook) {
    GUI.drawPopup(renderer, tr(STR_HARDCOVER_SETUP_HINT));
    startActivityForResult(std::make_unique<HardcoverSettingsActivity>(renderer, mappedInput),
                           [this](const ActivityResult&) {
                             HARDCOVER_STORE.loadFromFile();
                             requestUpdate();
                           });
    return;
  }

  if (action == LinkBook) {
    const std::string initial = bookId > 0 ? std::to_string(bookId) : "";
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_HARDCOVER_BOOK_ID),
                                                                   initial, 20, InputType::Text),
                           [this](const ActivityResult& result) {
                             if (!result.isCancelled) {
                               handleLinkInput(std::get<KeyboardResult>(result.data).text);
                             }
                             requestUpdate();
                           });
    return;
  }

  if (bookId <= 0) {
    if (action == AutoLink) {
      runAutoLink();
      return;
    }
    GUI.drawPopup(renderer, tr(STR_HARDCOVER_NOT_LINKED));
    requestUpdate();
    return;
  }

  if (action == AutoLink) {
    runAutoLink();
    return;
  }

  if (action == AutoSync) {
    autoSync = !autoSync;
    if (!HARDCOVER_LINKS.setAutoSync(epubPath, autoSync)) {
      autoSync = !autoSync;
    }
    requestUpdate();
    return;
  }

  if (action == Rate) {
    startActivityForResult(
        std::make_unique<IntervalSelectionActivity>(renderer, mappedInput, "HardcoverRate", StrId::STR_HARDCOVER_RATE,
                                                    StrId::STR_HARDCOVER_RATE_HINT, 5, 1, 5, 1, 1,
                                                    StrId::STR_HARDCOVER_RATING_FORMAT, true, true),
        [this](const ActivityResult& result) {
          if (!result.isCancelled) {
            runManualSync(Rate, static_cast<int>(std::get<IntervalResult>(result.data).value));
          }
          requestUpdate();
        });
    return;
  }

  runManualSync(action);
}

void HardcoverBookActivity::handleLinkInput(const std::string& input) {
  if (looksLikeIsbn(input)) {
    if (WiFi.status() != WL_CONNECTED) {
      GUI.drawPopup(renderer, tr(STR_WIFI_CONN_FAILED));
      return;
    }
    HardcoverBookSearchResult searchResult;
    const auto error = HardcoverClient::searchBook(input, searchResult);
    if (error == HardcoverClient::OK && searchResult.bookId > 0 &&
        HARDCOVER_LINKS.setLink(epubPath, searchResult.bookId, title)) {
      bookId = searchResult.bookId;
      autoSync = false;
      lastSyncedProgress = -1;
      GUI.drawPopup(renderer, tr(STR_HARDCOVER_AUTO_LINKED));
    } else {
      char errorBuffer[128];
      GUI.drawPopup(renderer, error == HardcoverClient::OK ? tr(STR_HARDCOVER_AUTO_LINK_FAILED)
                                                           : hardcoverErrorMessage(error, errorBuffer, sizeof(errorBuffer)));
    }
    return;
  }

  if (isPlainNumber(input)) {
    const int id = std::atoi(input.c_str());
    if (id > 0 && HARDCOVER_LINKS.setLink(epubPath, id, title)) {
      bookId = id;
      autoSync = false;
      lastSyncedProgress = -1;
    }
  }
}

void HardcoverBookActivity::runAutoLink() {
  if (WiFi.status() != WL_CONNECTED) {
    startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                           [this](const ActivityResult& result) {
                             if (!result.isCancelled) {
                               runAutoLink();
                             } else {
                               GUI.drawPopup(renderer, tr(STR_WIFI_CONN_FAILED));
                               requestUpdate();
                             }
                           });
    return;
  }

  GUI.drawPopup(renderer, tr(STR_HARDCOVER_SEARCHING));

  searchResults.clear();
  const auto error = HardcoverClient::searchBooks(title, author, searchResults, 5);
  if (error == HardcoverClient::OK && !searchResults.empty()) {
    selectingSearchResult = true;
    selectedSearchIndex = 0;
  } else {
    char errorBuffer[128];
    GUI.drawPopup(renderer, error == HardcoverClient::OK ? tr(STR_HARDCOVER_AUTO_LINK_FAILED)
                                                         : hardcoverErrorMessage(error, errorBuffer, sizeof(errorBuffer)));
  }
  requestUpdate();
}

void HardcoverBookActivity::confirmSearchResult() {
  if (selectedSearchIndex < 0 || selectedSearchIndex >= static_cast<int>(searchResults.size())) {
    selectingSearchResult = false;
    requestUpdate();
    return;
  }

  const HardcoverBookSearchResult& result = searchResults[selectedSearchIndex];
  if (result.bookId > 0 && HARDCOVER_LINKS.setLink(epubPath, result.bookId, title)) {
    bookId = result.bookId;
    autoSync = false;
    lastSyncedProgress = -1;
    selectingSearchResult = false;
    searchResults.clear();
    GUI.drawPopup(renderer, tr(STR_HARDCOVER_AUTO_LINKED));
  } else {
    GUI.drawPopup(renderer, tr(STR_HARDCOVER_AUTO_LINK_FAILED));
  }
  requestUpdate();
}

void HardcoverBookActivity::runManualSync(Action action, int rating) {
  if (WiFi.status() != WL_CONNECTED) {
    startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                           [this, action, rating](const ActivityResult& result) {
                             if (!result.isCancelled) {
                               runManualSync(action, rating);
                             } else {
                               GUI.drawPopup(renderer, tr(STR_WIFI_CONN_FAILED));
                               requestUpdate();
                             }
                           });
    return;
  }

  GUI.drawPopup(renderer, tr(STR_HARDCOVER_SYNCING));

  HardcoverClient::Error error = HardcoverClient::OK;
  switch (action) {
    case MarkReading:
      error = HardcoverClient::upsertBookStatus(bookId, 2);
      break;
    case UpdateProgress:
      error = HardcoverClient::updateProgress(bookId, progressPercent);
      break;
    case MarkRead:
      error = HardcoverClient::upsertBookStatus(bookId, 3);
      if (error == HardcoverClient::OK) {
        error = HardcoverClient::updateProgress(bookId, 100);
      }
      break;
    case Rate:
      error = HardcoverClient::rateBook(bookId, rating);
      break;
    case LinkBook:
    case AutoLink:
    case AutoSync:
    case Count:
      break;
  }

  char errorBuffer[128];
  GUI.drawPopup(renderer, error == HardcoverClient::OK ? tr(STR_HARDCOVER_SYNCED)
                                                       : hardcoverErrorMessage(error, errorBuffer, sizeof(errorBuffer)));
  if (error == HardcoverClient::OK && (action == UpdateProgress || action == MarkRead)) {
    const int syncedProgress = action == MarkRead ? 100 : progressPercent;
    if (HARDCOVER_LINKS.updateLastSyncedProgress(epubPath, syncedProgress)) {
      lastSyncedProgress = syncedProgress;
    }
  }
  requestUpdate();
}

void HardcoverBookActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);

  GUI.drawHeader(renderer, Rect{screen.x, screen.y + metrics.topPadding, screen.width, metrics.headerHeight},
                 tr(STR_HARDCOVER));
  char subheader[96];
  snprintf(subheader, sizeof(subheader), "%s: %s", bookId > 0 ? tr(STR_HARDCOVER_LINKED) : tr(STR_HARDCOVER_NOT_LINKED),
           bookId > 0 ? std::to_string(bookId).c_str() : "");
  GUI.drawSubHeader(renderer,
                    Rect{screen.x, screen.y + metrics.topPadding + metrics.headerHeight, screen.width,
                         metrics.tabBarHeight},
                    subheader);

  const int contentTop =
      screen.y + metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.verticalSpacing;
  const int contentHeight = screen.height - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  if (selectingSearchResult) {
    GUI.drawList(
        renderer, Rect{screen.x, contentTop, screen.width, contentHeight}, static_cast<int>(searchResults.size()),
        selectedSearchIndex,
        [this](int index) {
          const auto& result = searchResults[index];
          return result.title.empty() ? std::string(tr(STR_UNNAMED)) : result.title;
        },
        nullptr, nullptr,
        [this](int index) { return std::to_string(searchResults[index].bookId); }, true);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);
    renderer.displayBuffer();
    return;
  }

  int listTop = contentTop;
  int listHeight = contentHeight;
  if (!HARDCOVER_STORE.hasApiToken()) {
    const int textX = screen.x + metrics.contentSidePadding;
    int y = contentTop + 8;
    renderer.drawText(UI_10_FONT_ID, textX, y, tr(STR_HARDCOVER_SETUP_HINT));
    y += 28;
    renderer.drawText(UI_10_FONT_ID, textX, y, tr(STR_HARDCOVER_SETUP_HINT_2));
    y += 28;
    renderer.drawText(UI_10_FONT_ID, textX, y, tr(STR_HARDCOVER_SETUP_HINT_3));
    listTop += 100;
    listHeight -= 100;
  }
  GUI.drawList(
      renderer, Rect{screen.x, listTop, screen.width, listHeight}, Count, selectedIndex,
      [](int index) { return std::string(I18N.get(kMenuItems[index])); }, nullptr, nullptr,
      [this](int index) {
        if (index == UpdateProgress) return std::to_string(progressPercent) + "%";
        if (index == LinkBook && bookId > 0) return std::to_string(bookId);
        if (index == AutoSync) return std::string(autoSync ? tr(STR_STATE_ON) : tr(STR_STATE_OFF));
        return std::string("");
      },
      true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);
  renderer.displayBuffer();
}
