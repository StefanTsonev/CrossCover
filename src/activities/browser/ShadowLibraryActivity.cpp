#include "ShadowLibraryActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Memory.h>
#include <SdCardFontSystem.h>
#include <WiFi.h>

#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/home/FileBrowserActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
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
      selectedIndex = ButtonNavigator::nextPageIndex(selectedIndex, resultCount, PAGE_ITEMS);
      requestUpdate();
    });
    buttonNavigator.onPreviousContinuous([this] {
      selectedIndex = ButtonNavigator::previousPageIndex(selectedIndex, resultCount, PAGE_ITEMS);
      requestUpdate();
    });
  }
}

void ShadowLibraryActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  GUI.drawHeader(renderer, Rect{0, UITheme::getInstance().getMetrics().topPadding, pageWidth,
                                UITheme::getInstance().getMetrics().headerHeight},
                 tr(STR_SHADOW_LIBRARY));

  if (state == State::CHECK_WIFI || state == State::SEARCHING || state == State::SEARCH_INPUT) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, statusMessage.empty() ? tr(STR_LOADING) : statusMessage.c_str());
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

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  GUI.drawList(renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(resultCount), selectedIndex,
               [this](int index) { return results[index].title; },
               [this](int index) {
                 std::string metadata = results[index].author;
                 std::string details = results[index].size;
                 if (!results[index].downloads.empty()) {
                   if (!details.empty()) details += " · ";
                   details += results[index].downloads + " ";
                   details += tr(STR_SHADOW_LIBRARY_DOWNLOADS);
                 }
                 if (!details.empty()) metadata += "\n" + details;
                 return metadata;
               },
               nullptr, nullptr, false);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_DOWNLOAD), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
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
  if (filename.empty()) filename = "annas-archive-book";
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
