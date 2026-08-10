#include "HardcoverBookActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "HardcoverBookLinkStore.h"
#include "HardcoverClient.h"
#include "HardcoverCredentialStore.h"
#include "MappedInputManager.h"
#include "activities/util/IntervalSelectionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
const StrId kMenuItems[HardcoverBookActivity::Count] = {
    StrId::STR_HARDCOVER_LINK_BOOK,       StrId::STR_HARDCOVER_AUTO_LINK, StrId::STR_HARDCOVER_MARK_READING,
    StrId::STR_HARDCOVER_UPDATE_PROGRESS, StrId::STR_HARDCOVER_MARK_READ, StrId::STR_HARDCOVER_RATE,
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
}  // namespace

bool isPlainNumber(const std::string& text) {
  if (text.empty()) return false;
  return std::all_of(text.begin(), text.end(), [](const char c) { return c >= '0' && c <= '9'; });
}

}

void HardcoverBookActivity::onEnter() {
  Activity::onEnter();
  HARDCOVER_STORE.loadFromFile();
  HardcoverBookLink link;
  if (HARDCOVER_LINKS.getLink(epubPath, link)) {
    bookId = link.bookId;
    lastSyncedProgress = link.lastSyncedProgress;
  }
  requestUpdate();
}

void HardcoverBookActivity::loop() {
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

void HardcoverBookActivity::handleSelection() {
  const auto action = static_cast<Action>(selectedIndex);
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

  if (action == Rate) {
    startActivityForResult(
        std::make_unique<IntervalSelectionActivity>(renderer, mappedInput, "HardcoverRate", StrId::STR_HARDCOVER_RATE,
                                                    5, 1, 5, 1, 1, StrId::STR_HARDCOVER_RATING_FORMAT, true, true),
        [this](const ActivityResult& result) {
          if (!result.isCancelled) {
            queueAction(Rate, static_cast<int>(std::get<IntervalResult>(result.data).value));
          }
          requestUpdate();
        });
    return;
  }

  queueAction(action);
}

void HardcoverBookActivity::onExit() {
  Activity::onExit();
  if (auto* const fontCache = renderer.getFontCacheManager()) {
    fontCache->clearCache();
  }
  HardcoverClient::shutdownNetwork();
}

void HardcoverBookActivity::handleLinkInput(const std::string& input) {
  if (looksLikeIsbn(input)) {
    if (HARDCOVER_LINKS.queueLookup(epubPath, title, author, input)) {
      GUI.drawPopup(renderer, tr(STR_HARDCOVER_UPDATE_QUEUED));
    } else {
      GUI.drawPopup(renderer, tr(STR_HARDCOVER_QUEUE_FAILED));
    }
    return;
  }

  if (isPlainNumber(input)) {
    const int id = std::atoi(input.c_str());
    if (id > 0 && HARDCOVER_LINKS.setLink(epubPath, id, title)) {
      bookId = id;
      lastSyncedProgress = -1;
      GUI.drawPopup(renderer, tr(STR_HARDCOVER_LINKED));
    }
  }
}

void HardcoverBookActivity::runAutoLink() {
  if (HARDCOVER_LINKS.queueLookup(epubPath, title, author, "")) {
    GUI.drawPopup(renderer, tr(STR_HARDCOVER_UPDATE_QUEUED));
  } else {
    GUI.drawPopup(renderer, tr(STR_HARDCOVER_QUEUE_FAILED));
  }
  requestUpdate();
}

void HardcoverBookActivity::queueAction(Action action, int rating) {
  bool queued = false;
  switch (action) {
    case MarkReading:
      queued = HARDCOVER_LINKS.queueStatus(epubPath, 2);
      break;
    case UpdateProgress:
      queued = HARDCOVER_LINKS.queueProgress(epubPath, progressPercent);
      break;
    case MarkRead:
      queued = HARDCOVER_LINKS.queueStatus(epubPath, 3) && HARDCOVER_LINKS.queueProgress(epubPath, 100);
      break;
    case Rate:
      queued = HARDCOVER_LINKS.queueRating(epubPath, rating);
      break;
    case LinkBook:
    case AutoLink:
    case Count:
      break;
  }
  GUI.drawPopup(renderer, queued ? tr(STR_HARDCOVER_UPDATE_QUEUED) : tr(STR_HARDCOVER_QUEUE_FAILED));
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
  GUI.drawSubHeader(
      renderer,
      Rect{screen.x, screen.y + metrics.topPadding + metrics.headerHeight, screen.width, metrics.tabBarHeight},
      subheader);

  const int contentTop =
      screen.y + metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.verticalSpacing;
  const int contentHeight = screen.height - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
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
        return std::string("");
      },
      true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);
  renderer.displayBuffer();
}
