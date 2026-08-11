#include "HardcoverLibraryActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <cctype>
#include <cstdio>

#include "HardcoverCredentialStore.h"
#include "MappedInputManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/settings/HardcoverSettingsActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
const char* statusLabel(int statusId) {
  // Hardcover API status_id values; translated so non-English UIs don't show
  // hardcoded English in the library list.
  switch (statusId) {
    case 1:
      return tr(STR_HARDCOVER_STATUS_WANT);
    case 2:
      return tr(STR_HARDCOVER_STATUS_READING);
    case 3:
      return tr(STR_HARDCOVER_STATUS_READ);
    case 4:
      return tr(STR_HARDCOVER_STATUS_PAUSED);
    case 5:
      return tr(STR_HARDCOVER_STATUS_DNF);
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

void releaseHardcoverFontCaches(GfxRenderer& renderer, const char* reason) {
  auto* const fontCache = renderer.getFontCacheManager();
  if (!fontCache) return;

  const uint32_t freeBefore = ESP.getFreeHeap();
  const uint32_t maxBefore = ESP.getMaxAllocHeap();
  fontCache->clearCache();
  LOG_INF("HDC", "%s font cache release: free=%u->%u maxAlloc=%u->%u", reason, static_cast<unsigned>(freeBefore),
          static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(maxBefore),
          static_cast<unsigned>(ESP.getMaxAllocHeap()));
}

bool isNumericLookup(const std::string& value) {
  int digits = 0;
  for (const char c : value) {
    if (std::isdigit(static_cast<unsigned char>(c))) {
      digits++;
    } else if (c != '-' && c != ' ') {
      return false;
    }
  }
  return digits == 10 || digits == 13;
}
}  // namespace

void HardcoverLibraryActivity::onEnter() {
  Activity::onEnter();
  HARDCOVER_STORE.loadFromFile();
  refresh();
}

void HardcoverLibraryActivity::onExit() {
  Activity::onExit();
  releaseHardcoverFontCaches(renderer, "library exit");
  HardcoverClient::shutdownNetwork();
}

void HardcoverLibraryActivity::refresh() {
  loaded = false;
  HARDCOVER_LINKS.getPending(pending);
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
  GUI.drawPopup(renderer, tr(STR_LOADING));
  releaseHardcoverFontCaches(renderer, "library");
  lastError = HardcoverClient::fetchLibrary(books, 20);
  loaded = lastError == HardcoverClient::OK;
  requestUpdate();
}

void HardcoverLibraryActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (selectingLookup) {
      selectingLookup = false;
      lookupResults.clear();
      pendingLookupIndex = -1;
      requestUpdate();
      return;
    }
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (selectingLookup) {
      confirmLookup();
      return;
    }
    if (!HARDCOVER_STORE.hasApiToken()) {
      startActivityForResult(std::make_unique<HardcoverSettingsActivity>(renderer, mappedInput, true),
                             [this](const ActivityResult&) { refresh(); });
    } else if (!pending.empty() && selectedIndex == 0) {
      syncPending();
    } else {
      refresh();
    }
    return;
  }
  if (selectingLookup) {
    buttonNavigator.onNext([this] {
      if (!lookupResults.empty()) {
        selectedLookupIndex = (selectedLookupIndex + 1) % static_cast<int>(lookupResults.size());
        requestUpdate();
      }
    });
    buttonNavigator.onPrevious([this] {
      if (!lookupResults.empty()) {
        selectedLookupIndex =
            (selectedLookupIndex + static_cast<int>(lookupResults.size()) - 1) % static_cast<int>(lookupResults.size());
        requestUpdate();
      }
    });
    return;
  }
  const int count = static_cast<int>(books.size()) + (pending.empty() ? 0 : 1);
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

void HardcoverLibraryActivity::syncPending() {
  if (pending.empty()) {
    refresh();
    return;
  }

  GUI.drawPopup(renderer, tr(STR_HARDCOVER_SYNCING));
  renderer.displayBuffer();
  releaseHardcoverFontCaches(renderer, "pending sync");
  int sent = 0;
  for (size_t i = 0; i < pending.size(); ++i) {
    auto& item = pending[i];
    // Auto-link can be requested for an already-linked book. In that case the
    // existing bookId remains populated while pendingLookup asks us to find
    // and replace/confirm the link. Always process the lookup first.
    if (!item.pendingLookup.empty()) {
      lookupResults.clear();
      HardcoverClient::Error error = HardcoverClient::OK;
      if (isNumericLookup(item.pendingLookup)) {
        HardcoverBookSearchResult result;
        error = HardcoverClient::searchBook(item.pendingLookup, result);
        if (error == HardcoverClient::OK) lookupResults.push_back(result);
      } else {
        error = HardcoverClient::searchBooks(item.pendingLookup, item.pendingLookupAuthor, lookupResults, 5);
      }
      if (error != HardcoverClient::OK || lookupResults.empty()) continue;
      if (lookupResults.size() > 1) {
        pendingLookupIndex = static_cast<int>(i);
        selectedLookupIndex = 0;
        selectingLookup = true;
        requestUpdate();
        return;
      }
      if (!HARDCOVER_LINKS.setLink(item.path, lookupResults[0].bookId, item.title)) continue;
      item.bookId = lookupResults[0].bookId;
      lookupResults.clear();
    }
    if (item.bookId <= 0) continue;

    if (item.pendingStatusId > 0) {
      if (HardcoverClient::upsertBookStatus(item.bookId, item.pendingStatusId) != HardcoverClient::OK) continue;
      if (!HARDCOVER_LINKS.clearPendingStatus(item.path)) continue;
      sent++;
    }
    if (item.pendingProgress >= 0) {
      if (HardcoverClient::updateProgress(item.bookId, item.pendingProgress) != HardcoverClient::OK) continue;
      if (!HARDCOVER_LINKS.updateLastSyncedProgress(item.path, item.pendingProgress) ||
          !HARDCOVER_LINKS.clearPendingProgress(item.path))
        continue;
      sent++;
    }
    if (item.pendingRating > 0) {
      if (HardcoverClient::rateBook(item.bookId, item.pendingRating) != HardcoverClient::OK) continue;
      if (!HARDCOVER_LINKS.clearPendingRating(item.path)) continue;
      sent++;
    }
  }

  HARDCOVER_LINKS.getPending(pending);
  char summary[64];
  snprintf(summary, sizeof(summary), tr(STR_HARDCOVER_SYNC_SUMMARY), sent, static_cast<int>(pending.size()));
  refresh();
  GUI.drawPopup(renderer, summary);
  requestUpdate();
}

void HardcoverLibraryActivity::confirmLookup() {
  if (pendingLookupIndex < 0 || pendingLookupIndex >= static_cast<int>(pending.size()) || lookupResults.empty()) {
    selectingLookup = false;
    requestUpdate();
    return;
  }
  const auto& result = lookupResults[selectedLookupIndex];
  if (HARDCOVER_LINKS.setLink(pending[pendingLookupIndex].path, result.bookId, pending[pendingLookupIndex].title)) {
    selectingLookup = false;
    pendingLookupIndex = -1;
    lookupResults.clear();
    HARDCOVER_LINKS.getPending(pending);
    syncPending();
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

  if (selectingLookup) {
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(lookupResults.size()),
        selectedLookupIndex, [this](int index) { return lookupResults[index].title; }, nullptr, nullptr,
        [this](int index) { return std::to_string(lookupResults[index].bookId); }, true);
  } else if (!loaded) {
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
  } else if (books.empty() && pending.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_HARDCOVER_NO_BOOKS));
  } else {
    const bool hasPending = !pending.empty();
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(books.size()) + (hasPending ? 1 : 0),
        selectedIndex,
        [this, hasPending](int index) {
          if (hasPending && index == 0) {
            char label[64];
            snprintf(label, sizeof(label), tr(STR_HARDCOVER_SYNC_PENDING), static_cast<int>(pending.size()));
            return std::string(label);
          }
          return books[index - (hasPending ? 1 : 0)].title;
        },
        nullptr, nullptr,
        [this, hasPending](int index) {
          if (hasPending && index == 0) return std::string{};
          const auto& book = books[index - (hasPending ? 1 : 0)];
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

  const bool syncSelected = !pending.empty() && selectedIndex == 0;
  const StrId actionLabel = syncSelected ? StrId::STR_NEARBY_STATS_SYNC_BUTTON : StrId::STR_RELOAD;
  const auto labels =
      mappedInput.mapLabels(tr(STR_BACK), I18n::getInstance().get(actionLabel), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
