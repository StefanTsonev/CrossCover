#include "HardcoverLibraryActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

#include "HardcoverCredentialStore.h"
#include "MappedInputManager.h"
#include "SdCardFontSystem.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/settings/HardcoverSettingsActivity.h"
#include "activities/util/ConfirmationActivity.h"
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
  return tr(STR_HARDCOVER_UNKNOWN);
}

const char* hardcoverErrorMessage(HardcoverClient::Error error, const char* detail, char* buffer,
                                  const size_t bufferSize) {
  if (!detail[0]) {
    return HardcoverClient::errorString(error);
  }
  snprintf(buffer, bufferSize, "%s: %s", HardcoverClient::errorString(error), detail);
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
  // Replacement runs reader onExit() before this one-shot handoff starts.
  if (mode == Mode::RestartForBookSync || mode == Mode::RestartForBookAuth) {
    silentRestartToNetwork(
        mode == Mode::RestartForBookAuth ? NetworkBootTarget::HARDCOVER_AUTH : NetworkBootTarget::HARDCOVER_SYNC,
        mode == Mode::RestartForBookAuth ? 1 : 0);
    return;
  }
  if (mode == Mode::BookSyncNetwork || mode == Mode::LibraryNetwork) {
    sdFontSystem.releaseForNetwork(renderer);
    LOG_INF("HDC", "Minimal-boot book sync free=%u maxAlloc=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  }
  HARDCOVER_STORE.loadFromFile();
  refresh();
}

void HardcoverLibraryActivity::onExit() {
  Activity::onExit();
  releaseHardcoverFontCaches(renderer, "library exit");
  HardcoverClient::shutdownNetwork();
  if (mode == Mode::BookSyncNetwork)
    silentRestartToReader(true);
  else if (mode == Mode::LibraryNetwork)
    silentRestart();
}

void HardcoverLibraryActivity::refresh(bool showLoading) {
  if (showLoading) {
    RenderLock lock(*this);
    syncSummary[0] = '\0';
    syncResultVisible = false;
    operation = Operation::Loading;
    loaded = false;
  }
  if (!loadPending()) {
    {
      RenderLock lock(*this);
      setError(HardcoverClient::JSON_ERROR);
      loaded = false;
      operation = Operation::Idle;
    }
    requestUpdate();
    return;
  }
  if (!HARDCOVER_STORE.hasApiToken()) {
    {
      RenderLock lock(*this);
      setError(HardcoverClient::NO_TOKEN);
      loaded = false;
      operation = Operation::Idle;
    }
    requestUpdate();
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    if (!showLoading) {
      setError(HardcoverClient::NETWORK_ERROR);
      loaded = false;
      return;
    }
    auto wifi = makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput);
    if (!wifi) {
      LOG_ERR("HDC", "Could not allocate Wi-Fi selection activity");
      RenderLock lock(*this);
      setError(HardcoverClient::LOW_MEMORY);
      operation = Operation::Idle;
      requestUpdate();
      return;
    }
    startActivityForResult(std::move(wifi), [this](const ActivityResult& result) {
      if (!result.isCancelled) {
        refresh();
      } else {
        {
          RenderLock lock(*this);
          setError(HardcoverClient::NETWORK_ERROR);
          loaded = false;
          operation = Operation::Idle;
        }
        requestUpdate();
      }
    });
    return;
  }
  if (HARDCOVER_STORE.getUserId() <= 0) {
    if (requestUpdateAndWait() != RequestUpdateResult::Rendered) LOG_ERR("HDC", "Could not draw account check");
    const auto error = HardcoverClient::authenticate();
    if (error != HardcoverClient::OK) {
      RenderLock lock(*this);
      setError(error, HardcoverClient::lastErrorDetail());
      loaded = false;
      operation = Operation::Idle;
      requestUpdate();
      return;
    }
  }
  if (syncRequested) {
    syncRequested = false;
    syncPending();
    return;
  }
  if (showLoading) {
    if (requestUpdateAndWait() != RequestUpdateResult::Rendered) {
      LOG_ERR("HDC", "Library loading screen could not be rendered before request");
    }
  }
  releaseHardcoverFontCaches(renderer, "library");
  int scopedBookId = 0;
  if (!bookPath.empty()) {
    HardcoverBookLink link;
    if (HARDCOVER_LINKS.getLink(bookPath, link)) scopedBookId = link.bookId;
  }
  if (!bookPath.empty() && scopedBookId <= 0) {
    books.clear();
    setError(HardcoverClient::OK);
  } else {
    lastError = HardcoverClient::fetchLibrary(books, bookPath.empty() ? PAGE_SIZE + 1 : 1, scopedBookId,
                                              bookPath.empty() ? pageOffset : 0);
    setError(lastError, HardcoverClient::lastErrorDetail());
  }
  if (lastError == HardcoverClient::OK && !HARDCOVER_LINKS.cacheLibrary(books)) {
    LOG_ERR("HDC", "Could not persist confirmed library state");
    setError(HardcoverClient::JSON_ERROR);
  }
  if (bookPath.empty() && lastError == HardcoverClient::OK) {
    hasNextPage = books.size() > PAGE_SIZE;
    if (hasNextPage) books.resize(PAGE_SIZE);
  }
  loaded = lastError == HardcoverClient::OK;
  if (showLoading) {
    {
      RenderLock lock(*this);
      operation = Operation::Idle;
      const int count = static_cast<int>(books.size()) + (pending.empty() ? 0 : 1) + (pageOffset > 0 ? 1 : 0) +
                        (hasNextPage ? 1 : 0) + 1;
      selectedIndex = std::clamp(selectedIndex, 0, std::max(0, count - 1));
    }
    requestUpdate();
  }
}

void HardcoverLibraryActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (viewingDetails) {
      viewingDetails = false;
      mappedInput.suppressNextBackRelease();
      requestUpdate();
      return;
    }
    if (reviewingLookup) {
      reviewingLookup = false;
      mappedInput.suppressNextBackRelease();
      requestUpdate();
      return;
    }
    if (selectingLookup) {
      RenderLock lock(*this);
      selectingLookup = false;
      lookupResults.clear();
      pendingLookupIndex = -1;
      loaded = true;
      mappedInput.suppressNextBackRelease();
      requestUpdate();
      return;
    }
    finishAfterBackPress();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (syncResultVisible && !bookPath.empty()) {
      if (!pending.empty() || lastError != HardcoverClient::OK) syncPending();
      return;
    }
    if (viewingDetails) return;
    if (reviewingLookup) {
      reviewingLookup = false;
      confirmMatch();
      return;
    }
    if (selectingLookup) {
      confirmLookup();
      return;
    }
    if (!HARDCOVER_STORE.hasApiToken() || lastError == HardcoverClient::AUTH_FAILED) {
      auto settings = makeUniqueNoThrow<HardcoverSettingsActivity>(renderer, mappedInput, true);
      if (!settings) {
        LOG_ERR("HDC", "OOM opening account settings");
        setError(HardcoverClient::LOW_MEMORY);
        loaded = false;
        requestUpdate();
        return;
      }
      startActivityForResult(std::move(settings), [this](const ActivityResult&) { refresh(); });
    } else if (!pending.empty() && (selectedIndex == 0 || !loaded))
      syncPending();
    else if (loaded) {
      int row = selectedIndex - (pending.empty() ? 0 : 1);
      if (row >= 0 && row < static_cast<int>(books.size())) {
        detailIndex = row;
        {
          RenderLock lock(*this);
          operation = Operation::Loading;
        }
        requestUpdateAndWait();
        HardcoverBookSearchResult details;
        const auto error = HardcoverClient::getBookDetails(books[row].bookId, details);
        {
          RenderLock lock(*this);
          operation = Operation::Idle;
          if (error != HardcoverClient::OK) {
            setError(error, HardcoverClient::lastErrorDetail());
            loaded = false;
            requestUpdate();
            return;
          }
          books[row].author = std::move(details.author);
        }
        viewingDetails = true;
        requestUpdate();
      } else {
        row -= static_cast<int>(books.size());
        if (pageOffset > 0 && row == 0)
          pageOffset = std::max(0, pageOffset - PAGE_SIZE);
        else {
          if (pageOffset > 0) --row;
          if (hasNextPage && row == 0) pageOffset += PAGE_SIZE;
        }
        selectedIndex = 0;
        refresh();
      }
    } else
      refresh();
    return;
  }
  if (reviewingLookup) return;
  if (selectingLookup) {
    const int count = static_cast<int>(lookupResults.size());
    if (count <= 0) return;
    buttonNavigator.onNext([this, count] {
      selectedLookupIndex = ButtonNavigator::nextIndex(selectedLookupIndex, count);
      requestUpdate();
    });
    buttonNavigator.onPrevious([this, count] {
      selectedLookupIndex = ButtonNavigator::previousIndex(selectedLookupIndex, count);
      requestUpdate();
    });
    return;
  }
  if (viewingDetails || (syncResultVisible && !bookPath.empty()) || !loaded) return;
  const int count =
      static_cast<int>(books.size()) + (pending.empty() ? 0 : 1) + (pageOffset > 0 ? 1 : 0) + (hasNextPage ? 1 : 0) + 1;
  buttonNavigator.onNext([this, count] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, count);
    requestUpdate();
  });
  buttonNavigator.onPrevious([this, count] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, count);
    requestUpdate();
  });
}

void HardcoverLibraryActivity::syncPending() {
  {
    RenderLock lock(*this);
    if (syncResultVisible) {
      sentChanges = 0;
      sentUpdates = 0;
      syncFailed = false;
    }
    syncResultVisible = false;
    operation = Operation::Syncing;
    syncSummary[0] = '\0';
  }
  if (WiFi.status() != WL_CONNECTED || HARDCOVER_STORE.getUserId() <= 0) {
    syncRequested = true;
    refresh();
    return;
  }
  if (!loadPending()) {
    setError(HardcoverClient::JSON_ERROR);
    loaded = false;
    showSyncResult();
    return;
  }
  if (requestUpdateAndWait() != RequestUpdateResult::Rendered) LOG_ERR("HDC", "Could not draw sync screen");
  releaseHardcoverFontCaches(renderer, "pending sync");
  if (!syncFailed) setError(HardcoverClient::OK);
  bool failed = syncFailed;
  bool stopSession = false;
  const auto recordFailure = [this, &failed, &stopSession](HardcoverClient::Error error, const char* detail) {
    if (!failed) setError(error, detail);
    failed = true;
    syncFailed = true;
    // A book-specific API rejection may leave unrelated fields/books sendable.
    stopSession = error != HardcoverClient::API_ERROR;
  };
  for (size_t i = 0; i < pending.size() && !stopSession; ++i) {
    auto& item = pending[i];
    if (!item.pendingLookup.empty()) {
      lookupResults.clear();
      HardcoverClient::Error error;
      if (item.pendingLookup.rfind("id:", 0) == 0) {
        HardcoverBookSearchResult result;
        error = HardcoverClient::getBookDetails(std::atoi(item.pendingLookup.c_str() + 3), result);
        if (error == HardcoverClient::OK) lookupResults.push_back(std::move(result));
      } else if (isNumericLookup(item.pendingLookup)) {
        HardcoverBookSearchResult result;
        error = HardcoverClient::searchBook(item.pendingLookup, result);
        if (error == HardcoverClient::OK) lookupResults.push_back(std::move(result));
      } else
        error = HardcoverClient::searchBooks(item.pendingLookup, item.pendingLookupAuthor, lookupResults, 5);
      if (error != HardcoverClient::OK || lookupResults.empty()) {
        recordFailure(
            error == HardcoverClient::OK ? HardcoverClient::API_ERROR : error,
            error == HardcoverClient::OK ? tr(STR_HARDCOVER_AUTO_LINK_FAILED) : HardcoverClient::lastErrorDetail());
        continue;
      }
      pendingLookupIndex = static_cast<int>(i);
      selectedLookupIndex = 0;
      {
        RenderLock lock(*this);
        selectingLookup = true;
        operation = Operation::Idle;
      }
      if (lookupResults.size() == 1)
        confirmLookup();
      else
        requestUpdate();
      return;
    }
    if (item.bookId <= 0) {
      recordFailure(HardcoverClient::API_ERROR, tr(STR_HARDCOVER_NOT_LINKED));
      continue;
    }
    // Read confirmed remote state before writing; keep local pending intent intact.
    auto error = HardcoverClient::fetchLibrary(books, 1, item.bookId);
    if (error != HardcoverClient::OK) {
      recordFailure(error, HardcoverClient::lastErrorDetail());
      continue;
    }
    if (!HARDCOVER_LINKS.cacheLibrary(books)) {
      recordFailure(HardcoverClient::JSON_ERROR, "");
      continue;
    }
    if (item.pendingStatusId > 0) {
      error = HardcoverClient::upsertBookStatus(item.bookId, item.pendingStatusId);
      if (error != HardcoverClient::OK)
        recordFailure(error, HardcoverClient::lastErrorDetail());
      else if (!HARDCOVER_LINKS.confirmStatus(item.path, item.pendingStatusId))
        recordFailure(HardcoverClient::JSON_ERROR, "");
      else {
        ++sentChanges;
        ++sentUpdates;
      }
    }
    if (!stopSession && item.pendingProgress >= 0) {
      error = HardcoverClient::updateProgress(item.bookId, item.pendingProgress, item.editionId, item.editionPages);
      if (error != HardcoverClient::OK)
        recordFailure(error, HardcoverClient::lastErrorDetail());
      else if (!HARDCOVER_LINKS.confirmProgress(item.path, item.pendingProgress))
        recordFailure(HardcoverClient::JSON_ERROR, "");
      else {
        ++sentChanges;
        ++sentUpdates;
      }
    }
    if (!stopSession && item.pendingRating > 0) {
      const int status = item.pendingStatusId > 0 ? item.pendingStatusId : books.empty() ? 1 : books.front().statusId;
      error = HardcoverClient::rateBook(item.bookId, item.pendingRating, status);
      if (error != HardcoverClient::OK)
        recordFailure(error, HardcoverClient::lastErrorDetail());
      else if (!HARDCOVER_LINKS.confirmRating(item.path, item.pendingRating))
        recordFailure(HardcoverClient::JSON_ERROR, "");
      else {
        ++sentChanges;
        ++sentUpdates;
      }
    }
  }
  if (!loadPending()) {
    setError(HardcoverClient::JSON_ERROR);
    loaded = false;
    showSyncResult();
    return;
  }
  if (!failed && bookPath.empty())
    refresh(false);
  else if (failed)
    loaded = false;
  showSyncResult();
}

void HardcoverLibraryActivity::confirmLookup() {
  if (pendingLookupIndex < 0 || pendingLookupIndex >= static_cast<int>(pending.size()) || selectedLookupIndex < 0 ||
      selectedLookupIndex >= static_cast<int>(lookupResults.size())) {
    setError(HardcoverClient::API_ERROR);
    selectingLookup = false;
    showSyncResult();
    return;
  }
  {
    RenderLock lock(*this);
    operation = Operation::Loading;
  }
  requestUpdateAndWait();
  auto& match = lookupResults[selectedLookupIndex];
  HardcoverBookSearchResult metadata;
  const auto metadataError = HardcoverClient::getBookDetails(match.bookId, metadata);
  if (metadataError != HardcoverClient::OK) {
    setError(metadataError, HardcoverClient::lastErrorDetail());
    selectingLookup = false;
    showSyncResult();
    return;
  }
  match.author = std::move(metadata.author);
  if (match.editionId <= 0) {
    match.editionId = metadata.editionId;
    match.pages = metadata.pages;
  }
  const auto error = HardcoverClient::fetchLibrary(books, 1, match.bookId);
  if (error != HardcoverClient::OK) {
    setError(error, HardcoverClient::lastErrorDetail());
    selectingLookup = false;
    showSyncResult();
    return;
  }
  if (!books.empty() && books.front().editionId > 0 && books.front().pages > 0 &&
      !isNumericLookup(pending[pendingLookupIndex].pendingLookup)) {
    match.editionId = books.front().editionId;
    match.pages = books.front().pages;
  }
  {
    RenderLock lock(*this);
    operation = Operation::Idle;
    reviewingLookup = true;
  }
  requestUpdate();
}

void HardcoverLibraryActivity::confirmMatch() {
  const auto& match = lookupResults[selectedLookupIndex];
  const auto& item = pending[pendingLookupIndex];
  if (!HARDCOVER_LINKS.setLink(item.path, match.bookId, match.title, match.editionId, match.pages) ||
      !HARDCOVER_LINKS.cacheLibrary(books)) {
    setError(HardcoverClient::JSON_ERROR);
    selectingLookup = false;
    loadPending();
    showSyncResult();
    return;
  }
  {
    RenderLock lock(*this);
    selectingLookup = false;
    pendingLookupIndex = -1;
    lookupResults.clear();
    ++sentChanges;
    operation = Operation::Syncing;
  }
  syncPending();
}

void HardcoverLibraryActivity::setError(const HardcoverClient::Error error, const char* detail) {
  lastError = error;
  snprintf(errorDetail, sizeof(errorDetail), "%s", error == HardcoverClient::OK ? "" : detail);
  if (error == HardcoverClient::AUTH_FAILED && HARDCOVER_STORE.getUserId() > 0) {
    HARDCOVER_STORE.setUserInfo(0, "");
    if (!HARDCOVER_STORE.saveToFile()) LOG_ERR("HDC", "Could not persist expired authentication state");
  }
}

int HardcoverLibraryActivity::pendingChanges() const {
  int count = 0;
  for (const auto& item : pending) {
    count += item.pendingStatusId > 0;
    count += item.pendingProgress >= 0;
    count += item.pendingRating > 0;
    count += !item.pendingLookup.empty();
  }
  return count;
}

void HardcoverLibraryActivity::showSyncResult() {
  RenderLock lock(*this);
  snprintf(syncSummary, sizeof(syncSummary), tr(STR_HARDCOVER_SYNC_SUMMARY), sentChanges, pendingChanges());
  syncResultVisible = true;
  operation = Operation::Idle;
  if (!bookPath.empty())
    selectedIndex = (!pending.empty() || lastError != HardcoverClient::OK) ? 1 : 0;
  else {
    const int count = static_cast<int>(books.size()) + (pending.empty() ? 0 : 1) + (pageOffset > 0 ? 1 : 0) +
                      (hasNextPage ? 1 : 0) + 1;
    selectedIndex = std::clamp(selectedIndex, 0, std::max(0, count - 1));
  }
  requestUpdate();
}

bool HardcoverLibraryActivity::loadPending() {
  // A scoped sync materializes only this book's queue, rather than allocating
  // strings and vector entries for every other pending book on a C3.
  return HARDCOVER_LINKS.getPending(pending, bookPath);
}

void HardcoverLibraryActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const Rect header{screen.x, screen.y + metrics.topPadding, screen.width, metrics.headerHeight};
  GUI.drawHeader(renderer, header,
                 reviewingLookup    ? tr(STR_HARDCOVER_LINK_BOOK)
                 : bookPath.empty() ? tr(STR_HARDCOVER_LIBRARY)
                                    : tr(STR_HARDCOVER_SEND_BOOK));
  int contentTop = header.y + header.height + metrics.verticalSpacing;
  const int contentBottom = screen.y + screen.height - metrics.buttonHintsHeight - metrics.verticalSpacing;
  if (operation != Operation::Idle) {
    renderer.drawCenteredText(UI_10_FONT_ID, contentTop + std::max(0, contentBottom - contentTop) / 2,
                              operation == Operation::Syncing ? tr(STR_HARDCOVER_SYNCING) : tr(STR_LOADING));
    renderer.displayBuffer();
    return;
  }
  if (syncSummary[0] && !reviewingLookup) {
    const int height = std::max(metrics.tabBarHeight, renderer.getLineHeight(UI_12_FONT_ID) + 8);
    GUI.drawSubHeader(renderer, Rect{screen.x, contentTop, screen.width, height}, syncSummary);
    contentTop += height + metrics.verticalSpacing;
  }
  if (syncResultVisible && !bookPath.empty() && !selectingLookup) {
    char message[128];
    const char* resultText = lastError != HardcoverClient::OK
                                 ? hardcoverErrorMessage(lastError, errorDetail, message, sizeof(message))
                                 : (sentUpdates > 0   ? tr(STR_HARDCOVER_SYNCED)
                                    : sentChanges > 0 ? tr(STR_HARDCOVER_AUTO_LINKED)
                                                      : tr(STR_HARDCOVER_NO_CHANGE));
    const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
    const auto lines =
        renderer.wrappedText(UI_10_FONT_ID, resultText, std::max(1, screen.width - metrics.contentSidePadding * 2), 3);
    for (const auto& line : lines) {
      if (contentTop + lineHeight > contentBottom) break;
      renderer.drawText(UI_10_FONT_ID, screen.x + metrics.contentSidePadding, contentTop, line.c_str());
      contentTop += lineHeight;
    }
    contentTop = std::min(contentBottom, contentTop + metrics.verticalSpacing);
    const bool retry = !pending.empty() || lastError != HardcoverClient::OK;
    if (!pending.empty()) {
      char remaining[160] = {};
      const auto& item = pending.front();
      snprintf(remaining, sizeof(remaining), "%s%s%s%s",
               item.pendingStatusId > 0 ? tr(STR_HARDCOVER_PENDING_STATUS) : "",
               item.pendingProgress >= 0 ? tr(STR_HARDCOVER_PENDING_PROGRESS) : "",
               item.pendingRating > 0 ? tr(STR_HARDCOVER_PENDING_RATING) : "",
               !item.pendingLookup.empty() ? tr(STR_HARDCOVER_PENDING_LINK) : "");
      const auto pendingLines =
          renderer.wrappedText(UI_10_FONT_ID, remaining, std::max(1, screen.width - metrics.contentSidePadding * 2), 3);
      for (const auto& line : pendingLines) {
        if (contentTop + lineHeight > contentBottom) break;
        renderer.drawText(UI_10_FONT_ID, screen.x + metrics.contentSidePadding, contentTop, line.c_str());
        contentTop += lineHeight;
      }
    }
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), retry ? tr(STR_RETRY) : "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }
  const Rect content{screen.x, contentTop, screen.width, std::max(0, contentBottom - contentTop)};

  if (viewingDetails && detailIndex >= 0 && detailIndex < static_cast<int>(books.size())) {
    const auto& book = books[detailIndex];
    int y = contentTop;
    const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
    const int textWidth = std::max(1, screen.width - metrics.contentSidePadding * 2);
    const auto titleLines = renderer.wrappedText(UI_10_FONT_ID, book.title.c_str(), textWidth, 3);
    for (const auto& line : titleLines) {
      if (y + lineHeight > contentBottom) break;
      renderer.drawText(UI_10_FONT_ID, screen.x + metrics.contentSidePadding, y, line.c_str());
      y += lineHeight;
    }
    if (!book.author.empty() && y + lineHeight <= contentBottom) {
      const auto author = renderer.truncatedText(UI_10_FONT_ID, book.author.c_str(), textWidth);
      renderer.drawText(UI_10_FONT_ID, screen.x + metrics.contentSidePadding, y, author.c_str());
      y += lineHeight;
    }
    GUI.drawList(
        renderer,
        Rect{screen.x, y + metrics.verticalSpacing, screen.width,
             std::max(0, contentBottom - y - metrics.verticalSpacing)},
        3, -1,
        [](int index) {
          return std::string(index == 0   ? tr(STR_HARDCOVER_CHANGE_STATUS)
                             : index == 1 ? tr(STR_HARDCOVER_PROGRESS)
                                          : tr(STR_HARDCOVER_RATING));
        },
        nullptr, nullptr,
        [&book](int index) {
          char value[48];
          if (index == 0) return std::string(statusLabel(book.statusId));
          if (index == 1) {
            if (book.pages <= 0) return std::string(tr(STR_HARDCOVER_UNKNOWN));
            snprintf(value, sizeof(value), "%d/%d", book.progressPages, book.pages);
          } else {
            if (book.rating <= 0) return std::string(tr(STR_NOT_SET));
            snprintf(value, sizeof(value), tr(STR_HARDCOVER_HALF_RATING_FORMAT), static_cast<double>(book.rating));
          }
          return std::string(value);
        },
        false);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }
  if (reviewingLookup && selectedLookupIndex >= 0 && selectedLookupIndex < static_cast<int>(lookupResults.size())) {
    const auto& match = lookupResults[selectedLookupIndex];
    const int x = screen.x + metrics.contentSidePadding;
    const int width = std::max(1, screen.width - metrics.contentSidePadding * 2);
    const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID) + 4;
    // Reserve the metadata rows and confirmation note before wrapping headings,
    // so landscape layouts do not hide the edition behind an inert list page.
    const int headingLines = std::max(
        2, (contentBottom - contentTop - metrics.listRowHeight * 5 - lineHeight * 2 - metrics.verticalSpacing * 2) /
               lineHeight);
    const int titleLineCount = std::min(3, headingLines - 1);
    int y = contentTop;
    for (const auto& line : renderer.wrappedText(UI_10_FONT_ID, match.title.c_str(), width, titleLineCount)) {
      if (y + lineHeight > contentBottom) break;
      renderer.drawText(UI_10_FONT_ID, x, y, line.c_str());
      y += lineHeight;
    }
    for (const auto& line :
         renderer.wrappedText(UI_10_FONT_ID, match.author.c_str(), width, std::min(2, headingLines - titleLineCount))) {
      if (y + lineHeight > contentBottom) break;
      renderer.drawText(UI_10_FONT_ID, x, y, line.c_str());
      y += lineHeight;
    }
    const int noteTop = contentBottom - lineHeight * 2;
    GUI.drawList(
        renderer,
        Rect{screen.x, y + metrics.verticalSpacing, screen.width,
             std::max(0, noteTop - y - metrics.verticalSpacing * 2)},
        5, -1,
        [](int index) {
          const StrId labels[] = {StrId::STR_HARDCOVER_CHANGE_STATUS, StrId::STR_HARDCOVER_PROGRESS,
                                  StrId::STR_HARDCOVER_RATING, StrId::STR_HARDCOVER_BOOK_ID_LABEL,
                                  StrId::STR_HARDCOVER_EDITION};
          return std::string(I18N.get(labels[index]));
        },
        nullptr, nullptr,
        [this, &match](int index) {
          char value[64];
          if (index == 0)
            return std::string(books.empty() ? tr(STR_HARDCOVER_NOT_IN_LIBRARY) : statusLabel(books.front().statusId));
          if (index == 1) {
            if (books.empty() || books.front().pages <= 0) return std::string(tr(STR_HARDCOVER_UNKNOWN));
            snprintf(value, sizeof(value), "%d/%d", books.front().progressPages, books.front().pages);
          } else if (index == 2) {
            if (books.empty() || books.front().rating <= 0) return std::string(tr(STR_NOT_SET));
            snprintf(value, sizeof(value), tr(STR_HARDCOVER_HALF_RATING_FORMAT),
                     static_cast<double>(books.front().rating));
          } else if (index == 3)
            snprintf(value, sizeof(value), "#%d", match.bookId);
          else {
            if (match.editionId <= 0) return std::string(tr(STR_HARDCOVER_UNKNOWN));
            snprintf(value, sizeof(value), tr(STR_HARDCOVER_EDITION_VALUE), match.editionId, match.pages);
          }
          return std::string(value);
        },
        false);
    int noteY = std::max(y, noteTop);
    for (const auto& line : renderer.wrappedText(UI_10_FONT_ID, tr(STR_HARDCOVER_MATCH_SEND), width, 2)) {
      if (noteY + lineHeight > contentBottom) break;
      renderer.drawText(UI_10_FONT_ID, x, noteY, line.c_str());
      noteY += lineHeight;
    }
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_CONFIRM), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }
  if (selectingLookup) {
    GUI.drawList(
        renderer, content, static_cast<int>(lookupResults.size()), selectedLookupIndex,
        [this](int index) { return lookupResults[index].title; },
        [this](int index) { return lookupResults[index].author; }, nullptr,
        [this](int index) { return std::string("#") + std::to_string(lookupResults[index].bookId); }, true);
  } else if (!loaded) {
    const int textX = screen.x + metrics.contentSidePadding;
    const int textWidth = std::max(1, screen.width - metrics.contentSidePadding * 2);
    int y = contentTop + metrics.verticalSpacing;
    if (lastError == HardcoverClient::NO_TOKEN) {
      const StrId hints[] = {StrId::STR_HARDCOVER_SETUP_HINT, StrId::STR_HARDCOVER_TOKEN_PATH,
                             StrId::STR_HARDCOVER_SETUP_HINT_2, StrId::STR_HARDCOVER_SETUP_HINT_3};
      for (const auto hint : hints) {
        const int maxLines = std::max(1, (contentBottom - y) / renderer.getLineHeight(UI_10_FONT_ID));
        const auto lines = renderer.wrappedText(UI_10_FONT_ID, I18N.get(hint), textWidth, maxLines);
        for (const auto& line : lines) {
          const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
          if (y + lineHeight > contentBottom) break;
          renderer.drawText(UI_10_FONT_ID, textX, y, line.c_str());
          y += lineHeight;
        }
        y += metrics.verticalSpacing;
      }
    } else {
      char errorBuffer[128];
      const int maxLines = std::max(1, (contentBottom - y) / renderer.getLineHeight(UI_10_FONT_ID));
      const auto lines = renderer.wrappedText(
          UI_10_FONT_ID, hardcoverErrorMessage(lastError, errorDetail, errorBuffer, sizeof(errorBuffer)), textWidth,
          maxLines);
      for (const auto& line : lines) {
        const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
        if (y + lineHeight > contentBottom) break;
        renderer.drawText(UI_10_FONT_ID, textX, y, line.c_str());
        y += lineHeight;
      }
    }
  } else if (books.empty() && pending.empty() && pageOffset == 0) {
    renderer.drawCenteredText(UI_10_FONT_ID, contentTop + content.height / 2, tr(STR_HARDCOVER_NO_BOOKS));
  } else {
    const bool hasPending = !pending.empty();
    GUI.drawList(
        renderer, content,
        static_cast<int>(books.size()) + (hasPending ? 1 : 0) + (pageOffset > 0 ? 1 : 0) + (hasNextPage ? 1 : 0) + 1,
        selectedIndex,
        [this, hasPending](int index) {
          if (hasPending && index == 0) {
            char label[96];
            snprintf(label, sizeof(label), tr(STR_HARDCOVER_SYNC_PENDING), pendingChanges());
            return std::string(label);
          }
          int row = index - (hasPending ? 1 : 0);
          if (row < static_cast<int>(books.size())) return books[row].title;
          row -= static_cast<int>(books.size());
          if (pageOffset > 0) {
            if (row == 0) return std::string(tr(STR_HARDCOVER_PREVIOUS_PAGE));
            --row;
          }
          if (hasNextPage) {
            if (row == 0) return std::string(tr(STR_HARDCOVER_NEXT_PAGE));
            --row;
          }
          return std::string(tr(STR_RELOAD));
        },
        [this, hasPending](int index) {
          if (hasPending && index == 0) return std::string(tr(STR_HARDCOVER_UPDATE_QUEUED));
          const int row = index - (hasPending ? 1 : 0);
          if (row >= static_cast<int>(books.size())) return std::string{};
          const auto& book = books[row];
          std::string subtitle = book.author;
          if (!subtitle.empty()) subtitle += " | ";
          subtitle += statusLabel(book.statusId);
          return subtitle;
        },
        nullptr,
        [this, hasPending](int index) {
          if (hasPending && index == 0) return std::string{};
          const int row = index - (hasPending ? 1 : 0);
          if (row >= static_cast<int>(books.size())) return std::string{};
          const auto& book = books[row];
          char value[32] = {};
          if (book.pages > 0 && book.progressPages > 0) {
            snprintf(value, sizeof(value), "%d/%d", book.progressPages, book.pages);
          } else if (book.rating > 0) {
            snprintf(value, sizeof(value), "%.1f*", static_cast<double>(book.rating));
          }
          return std::string(value);
        },
        true);
  }

  const bool syncSelected = !selectingLookup && !pending.empty() && (selectedIndex == 0 || !loaded);
  const StrId actionLabel = selectingLookup ? StrId::STR_SELECT
                            : lastError == HardcoverClient::NO_TOKEN || lastError == HardcoverClient::AUTH_FAILED
                                ? StrId::STR_HARDCOVER_SETUP
                            : syncSelected ? StrId::STR_NEARBY_STATS_SYNC_BUTTON
                            : loaded       ? StrId::STR_SELECT
                                           : StrId::STR_RETRY;
  const bool canNavigate = selectingLookup || (loaded && (!books.empty() || !pending.empty() || pageOffset > 0));
  const auto labels = mappedInput.mapLabels(
      tr(STR_BACK),
      loaded && books.empty() && pending.empty() && pageOffset == 0 ? tr(STR_RELOAD) : I18N.get(actionLabel),
      canNavigate ? tr(STR_DIR_UP) : "", canNavigate ? tr(STR_DIR_DOWN) : "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
