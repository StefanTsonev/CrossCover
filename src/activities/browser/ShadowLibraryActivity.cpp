#include "ShadowLibraryActivity.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <JpegToBmpConverter.h>
#include <Memory.h>
#include <SdCardFontSystem.h>
#include <WiFi.h>

#include <algorithm>
#include <cctype>

#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/home/FileBrowserActivity.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"
#include "network/HttpDownloader.h"
#include "network/ShadowLibrarySettings.h"
#include "util/BookCacheUtils.h"
#include "util/StringUtils.h"

void ShadowLibraryActivity::onEnter() {
  Activity::onEnter();
  sdFontSystem.releaseLoadedFont(renderer);
  state = State::CHECK_WIFI;
  resultCount = 0;
  selectedIndex = 0;
  consumeConfirm = false;
  errorMessage.clear();
  statusMessage = tr(STR_CHECKING_WIFI);
  requestUpdate();

  results = makeUniqueNoThrow<ShadowLibraryBook[]>(MAX_SHADOW_LIBRARY_RESULTS);
  if (!results) {
    state = State::ERROR;
    errorMessage = tr(STR_MEMORY_ERROR);
    requestUpdate();
    return;
  }
  ShadowLibrarySettings::instance().loadFromFile();
  checkAndConnectWifi();
}

void ShadowLibraryActivity::onExit() {
  Activity::onExit();
  results.reset();
  resultCount = 0;
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

bool ShadowLibraryActivity::preventAutoSleep() {
  return state == State::CHECK_WIFI || state == State::WIFI_SELECTION || state == State::SEARCH_INPUT ||
         state == State::SEARCHING || state == State::DOWNLOADING;
}

void ShadowLibraryActivity::loop() {
  if (state == State::WIFI_SELECTION || state == State::SEARCH_INPUT || state == State::SEARCHING ||
      state == State::DOWNLOADING) {
    return;
  }

  if (consumeConfirm && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    consumeConfirm = false;
    return;
  }

  if (state == State::ERROR) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      checkAndConnectWifi();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      onGoHome();
    }
    return;
  }

  if (state == State::CHECK_WIFI) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) onGoHome();
    return;
  }

  if (state != State::BROWSING) return;

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (resultCount > 0 && selectedIndex >= 0 && selectedIndex < static_cast<int>(resultCount)) {
      downloadBook(results[selectedIndex]);
    }
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome();
  }

  if (resultCount > 0) {
    buttonNavigator.onNext([this] {
      selectedIndex = ButtonNavigator::nextIndex(selectedIndex, resultCount);
      requestUpdate();
    });
    buttonNavigator.onPrevious([this] {
      selectedIndex = ButtonNavigator::previousIndex(selectedIndex, resultCount);
      requestUpdate();
    });
    buttonNavigator.onNextContinuous([this] {
      selectedIndex = ButtonNavigator::nextPageIndex(selectedIndex, resultCount, itemsPerPage());
      requestUpdate();
    });
    buttonNavigator.onPreviousContinuous([this] {
      selectedIndex = ButtonNavigator::previousPageIndex(selectedIndex, resultCount, itemsPerPage());
      requestUpdate();
    });
  }
}

int ShadowLibraryActivity::itemsPerPage() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  // Leave room for the six-pixel selection outline above and below each cover.
  return std::clamp(contentHeight / (COVER_HEIGHT + 20), 1, PAGE_ITEMS);
}

void ShadowLibraryActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  GUI.drawHeader(renderer,
                 Rect{0, UITheme::getInstance().getMetrics().topPadding, pageWidth,
                      UITheme::getInstance().getMetrics().headerHeight},
                 tr(STR_SHADOW_LIBRARY));

  if (state == State::CHECK_WIFI || state == State::SEARCHING || state == State::SEARCH_INPUT) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2,
                              statusMessage.empty() ? tr(STR_LOADING) : statusMessage.c_str());
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == State::ERROR) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 20, tr(STR_ERROR_MSG));
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 10, errorMessage.c_str());
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_RETRY), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == State::DOWNLOADING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 40, tr(STR_DOWNLOADING));
    const auto title = renderer.truncatedText(UI_10_FONT_ID, statusMessage.c_str(), pageWidth - 40);
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 10, title.c_str());
    if (downloadTotal > 0) {
      GUI.drawProgressBar(renderer, Rect{50, pageHeight / 2 + 20, pageWidth - 100, 20}, downloadProgress,
                          downloadTotal);
    }
    const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (resultCount == 0) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_SHADOW_LIBRARY_NO_RESULTS));
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  const int pageItems = itemsPerPage();
  const int pageStart = (selectedIndex / pageItems) * pageItems;
  const int cardHeight = contentHeight / pageItems;
  const int visibleCount = std::min(pageItems, static_cast<int>(resultCount) - pageStart);
  for (int row = 0; row < visibleCount; ++row) {
    const int index = pageStart + row;
    renderResultCard(index, 0, contentTop + row * cardHeight, pageWidth, cardHeight, index == selectedIndex);
  }
  const int totalPages = (static_cast<int>(resultCount) + pageItems - 1) / pageItems;
  if (totalPages > 1) {
    constexpr int dotSize = 8;
    constexpr int dotSpacing = 6;
    const int totalDotWidth = totalPages * dotSize + (totalPages - 1) * dotSpacing;
    const int dotsStartX = (pageWidth - totalDotWidth) / 2;
    const int dotY = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing - 4;
    const int currentPage = selectedIndex / pageItems;
    for (int page = 0; page < totalPages; ++page) {
      const int dotX = dotsStartX + page * (dotSize + dotSpacing);
      if (page == currentPage) {
        renderer.fillRect(dotX, dotY, dotSize, dotSize, true);
      } else {
        renderer.drawRect(dotX, dotY, dotSize, dotSize, true);
      }
    }
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_DOWNLOAD), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void ShadowLibraryActivity::renderResultCard(const int index, const int x, const int y, const int width,
                                             const int height, const bool selected) const {
  const auto& book = results[index];
  constexpr int coverCornerRadius = 2;
  constexpr int selectionPadding = 4;
  constexpr int selectionOuterInset = 6;
  const bool textBlack = true;

  const int coverX = x + 12;
  const int coverY = y + std::max(4, (height - COVER_HEIGHT) / 2);
  bool coverDrawn = false;
  if (!book.coverBmpPath.empty() && Storage.exists(book.coverBmpPath.c_str())) {
    FsFile file;
    if (Storage.openFileForRead("SLIB", book.coverBmpPath, file)) {
      Bitmap bitmap(file);
      if (bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.getWidth() > 0 && bitmap.getHeight() > 0) {
        renderer.fillRoundedRect(coverX, coverY, COVER_WIDTH, COVER_HEIGHT, coverCornerRadius, Color::White);
        renderer.drawBitmap(bitmap, coverX, coverY, COVER_WIDTH, COVER_HEIGHT);
        renderer.maskRoundedRectOutsideCorners(coverX, coverY, COVER_WIDTH, COVER_HEIGHT, coverCornerRadius,
                                               Color::White);
        renderer.drawRoundedRect(coverX, coverY, COVER_WIDTH, COVER_HEIGHT, 2, coverCornerRadius, true);
        coverDrawn = true;
      }
      file.close();
    }
  }
  if (!coverDrawn) {
    renderer.fillRoundedRect(coverX, coverY, COVER_WIDTH, COVER_HEIGHT, coverCornerRadius, Color::White);
    renderer.drawRoundedRect(coverX, coverY, COVER_WIDTH, COVER_HEIGHT, 2, coverCornerRadius, true);
    drawLucideIcon(renderer, icon_book_marked_32, coverX + (COVER_WIDTH - 32) / 2, coverY + (COVER_HEIGHT - 32) / 2);
  }

  if (selected) {
    renderer.drawRoundedRect(coverX - selectionPadding, coverY - selectionPadding, COVER_WIDTH + selectionPadding * 2,
                             COVER_HEIGHT + selectionPadding * 2, 3, coverCornerRadius + selectionPadding, true);
    renderer.drawRoundedRect(coverX - selectionOuterInset, coverY - selectionOuterInset,
                             COVER_WIDTH + selectionOuterInset * 2, COVER_HEIGHT + selectionOuterInset * 2, 1,
                             coverCornerRadius + selectionOuterInset, true);
  }

  const int textX = coverX + COVER_WIDTH + 18;
  const int textWidth = x + width - textX - 12;
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const auto titleLines = renderer.wrappedText(UI_10_FONT_ID, book.title.c_str(), textWidth, 2, EpdFontFamily::BOLD);
  int textY = coverY;
  for (const auto& line : titleLines) {
    renderer.drawText(UI_10_FONT_ID, textX, textY, line.c_str(), textBlack, EpdFontFamily::BOLD);
    textY += lineHeight;
  }
  if (!book.author.empty()) {
    const auto author = renderer.truncatedText(UI_10_FONT_ID, book.author.c_str(), textWidth);
    renderer.drawText(UI_10_FONT_ID, textX, textY, author.c_str(), textBlack);
    textY += lineHeight;
  }

  textY += 8;
  std::string edition = book.language;
  if (!book.year.empty()) edition += (edition.empty() ? "" : " · ") + book.year;
  if (!edition.empty()) {
    const auto line = renderer.truncatedText(UI_10_FONT_ID, edition.c_str(), textWidth);
    renderer.drawText(UI_10_FONT_ID, textX, textY, line.c_str(), textBlack);
    textY += lineHeight;
  }
  std::string fileInfo = book.format;
  std::transform(fileInfo.begin(), fileInfo.end(), fileInfo.begin(),
                 [](const unsigned char c) { return static_cast<char>(std::toupper(c)); });
  if (!book.size.empty()) fileInfo += (fileInfo.empty() ? "" : " · ") + book.size;
  if (!fileInfo.empty()) {
    const auto line = renderer.truncatedText(UI_10_FONT_ID, fileInfo.c_str(), textWidth);
    renderer.drawText(UI_10_FONT_ID, textX, textY, line.c_str(), textBlack);
  }
}

void ShadowLibraryActivity::prepareCoverThumbnails() {
  if (!Storage.ensureDirectoryExists(COVER_CACHE_DIR)) {
    LOG_ERR("SHADOW", "Failed to create LibGen cover cache directory");
    return;
  }

  for (size_t i = 0; i < resultCount; ++i) {
    auto& book = results[i];
    book.coverBmpPath.clear();
    if (book.coverUrl.empty() || book.md5.empty()) continue;

    const std::string basePath = std::string(COVER_CACHE_DIR) + "/" + book.md5;
    const std::string jpgPath = basePath + ".jpg";
    const std::string bmpPath = basePath + ".bmp";
    book.coverBmpPath = bmpPath;
    if (Storage.exists(bmpPath.c_str())) {
      FsFile cachedBmp;
      bool validCache = Storage.openFileForRead("SLIB", bmpPath, cachedBmp);
      if (validCache) {
        Bitmap bitmap(cachedBmp);
        validCache = bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.getWidth() == COVER_WIDTH &&
                     bitmap.getHeight() == COVER_HEIGHT;
        cachedBmp.close();
      }
      if (validCache) continue;
      LOG_DBG("SHADOW", "Removing invalid LibGen cover cache for %s", book.md5.c_str());
      Storage.remove(bmpPath.c_str());
    }

    HttpDownloader::DownloadOptions options;
    options.bufferSize = 1024;
    options.transport = HttpDownloader::Transport::WOLFSSL;
    const auto result = HttpDownloader::downloadToFile(book.coverUrl, jpgPath, nullptr, nullptr, "", "", options);
    if (result != HttpDownloader::OK) {
      LOG_ERR("SHADOW", "Failed to download LibGen cover for %s", book.md5.c_str());
      Storage.remove(jpgPath.c_str());
      book.coverBmpPath.clear();
      continue;
    }

    FsFile jpg;
    FsFile bmp;
    bool success = Storage.openFileForRead("SLIB", jpgPath, jpg) && Storage.openFileForWrite("SLIB", bmpPath, bmp);
    if (success) {
      success = JpegToBmpConverter::jpegFileTo1BitBmpStreamWithSize(jpg, bmp, COVER_WIDTH, COVER_HEIGHT, true);
    }
    jpg.close();
    bmp.close();
    Storage.remove(jpgPath.c_str());
    if (!success) {
      LOG_ERR("SHADOW", "Failed to convert LibGen cover for %s", book.md5.c_str());
      Storage.remove(bmpPath.c_str());
      book.coverBmpPath.clear();
    }
  }
}

void ShadowLibraryActivity::checkAndConnectWifi() {
  if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
    launchSearch();
    return;
  }
  launchWifiSelection();
}

void ShadowLibraryActivity::launchWifiSelection() {
  state = State::WIFI_SELECTION;
  requestUpdate();
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled) {
                             state = State::ERROR;
                             errorMessage = tr(STR_WIFI_CONN_FAILED);
                             requestUpdate();
                           } else {
                             launchSearch();
                           }
                         });
}

void ShadowLibraryActivity::launchSearch() {
  consumeConfirm = true;
  state = State::SEARCH_INPUT;
  statusMessage = tr(STR_SHADOW_LIBRARY_SEARCH_HINT);
  requestUpdate();
  auto keyboard = std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_SHADOW_LIBRARY_SEARCH), "", 96,
                                                          InputType::Text, 1);
  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
    if (result.isCancelled) {
      onGoHome();
      return;
    }
    consumeConfirm = false;
    performSearch(std::get<KeyboardResult>(result.data).text);
  });
}

void ShadowLibraryActivity::performSearch(const std::string& query) {
  state = State::SEARCHING;
  statusMessage = tr(STR_LOADING);
  requestUpdateAndWait();
  resultCount = 0;
  selectedIndex = 0;
  if (!ShadowLibraryClient::search(query, results.get(), MAX_SHADOW_LIBRARY_RESULTS, resultCount)) {
    state = State::ERROR;
    // The client logs whether this was transport or parsing failure. Keep the
    // UI honest here: an empty result and a TLS/network failure are not the
    // same as a valid search with no matches.
    errorMessage = tr(STR_SHADOW_LIBRARY_SEARCH_FAILED);
  } else {
    if (resultCount > 0) prepareCoverThumbnails();
    state = State::BROWSING;
  }
  requestUpdate();
}

void ShadowLibraryActivity::downloadBook(const ShadowLibraryBook& book) {
  state = State::DOWNLOADING;
  statusMessage = book.title;
  downloadProgress = downloadTotal = 0;
  requestUpdate(true);

  std::string downloadUrl;
  if (!ShadowLibraryClient::resolveDownloadUrl(book, downloadUrl)) {
    state = State::ERROR;
    errorMessage = tr(STR_SHADOW_LIBRARY_NO_MIRROR);
    requestUpdate();
    return;
  }

  std::string filename = StringUtils::sanitizeFilename(book.title);
  if (filename.empty()) filename = "libgen-book";
  const auto& directory = ShadowLibrarySettings::instance().downloadDirectory();
  filename = directory == "/" ? "/" + filename : directory + "/" + filename;
  filename += "." + (book.format.empty() ? "epub" : book.format);
  bool cancelRequested = false;
  auto pollCancel = [this, &cancelRequested] {
    mappedInput.update();
    if (mappedInput.isPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      cancelRequested = true;
    }
    return cancelRequested;
  };
  HttpDownloader::DownloadOptions options;
  options.shouldCancel = pollCancel;
  options.bufferSize = DOWNLOAD_BUFFER_SIZE;
  options.transport = HttpDownloader::Transport::WOLFSSL;
  const auto result = HttpDownloader::downloadToFile(
      downloadUrl, filename,
      [this](size_t downloaded, size_t total) {
        downloadProgress = downloaded;
        downloadTotal = total;
        requestUpdate(true);
      },
      &cancelRequested, "", "", options);

  if (result == HttpDownloader::OK) {
    clearBookCache(filename);
    state = State::BROWSING;
  } else if (result == HttpDownloader::ABORTED) {
    mappedInput.suppressNextBackRelease();
    state = State::BROWSING;
  } else {
    state = State::ERROR;
    errorMessage = tr(STR_DOWNLOAD_FAILED);
  }
  requestUpdate();
}
