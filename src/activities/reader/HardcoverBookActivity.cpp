#include "HardcoverBookActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <limits>

#include "HardcoverClient.h"
#include "HardcoverCredentialStore.h"
#include "MappedInputManager.h"
#include "activities/settings/HardcoverSettingsActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "activities/util/OptionSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
static constexpr StrId kMenuItems[] = {StrId::STR_HARDCOVER_CHANGE_STATUS, StrId::STR_HARDCOVER_SAVE_PROGRESS,
                                       StrId::STR_HARDCOVER_RATE,          StrId::STR_HARDCOVER_MATCHED_BOOK,
                                       StrId::STR_HARDCOVER_SEND_BOOK,     StrId::STR_HARDCOVER_SETUP,
                                       StrId::STR_HARDCOVER_REVIEW_PENDING};
static constexpr StrId kStatusLabels[] = {StrId::STR_HARDCOVER_UNKNOWN,        StrId::STR_HARDCOVER_STATUS_WANT,
                                          StrId::STR_HARDCOVER_STATUS_READING, StrId::STR_HARDCOVER_STATUS_READ,
                                          StrId::STR_HARDCOVER_STATUS_PAUSED,  StrId::STR_HARDCOVER_STATUS_DNF};
const char* statusLabel(int status) { return I18N.get(kStatusLabels[status >= 1 && status <= 5 ? status : 0]); }
bool looksLikeIsbn(const std::string& text) {
  int digits = 0;
  for (char c : text) {
    if (c >= '0' && c <= '9')
      ++digits;
    else if (c != '-' && c != ' ')
      return false;
  }
  return digits == 10 || digits == 13;
}
}  // namespace

void HardcoverBookActivity::onEnter() {
  Activity::onEnter();
  HARDCOVER_STORE.loadFromFile();
  reloadState();
  requestUpdate();
}
void HardcoverBookActivity::reloadState() {
  HardcoverBookLink updated;
  HARDCOVER_LINKS.getLink(epubPath, updated);
  RenderLock lock(*this);
  progressPercent = std::clamp(progressPercent, 0, 100);
  link = std::move(updated);
  hasPendingUpdate =
      link.pendingStatusId > 0 || link.pendingProgress >= 0 || link.pendingRating > 0 || !link.pendingLookup.empty();
  actionCount = 0;
  if (!HARDCOVER_STORE.hasApiToken())
    actions[actionCount++] = Setup;
  else {
    actions[actionCount++] = Status;
    actions[actionCount++] = UpdateProgress;
    actions[actionCount++] = Rate;
    actions[actionCount++] = LinkBook;
    if (HARDCOVER_STORE.getUserId() <= 0) actions[actionCount++] = Setup;
    if (hasPendingUpdate) {
      actions[actionCount++] = Sync;
      actions[actionCount++] = Pending;
    }
  }
  selectedIndex = std::clamp(selectedIndex, 0, std::max(0, actionCount - 1));
}
int HardcoverBookActivity::effectiveStatus() const {
  return link.pendingStatusId > 0 ? link.pendingStatusId : link.lastSyncedStatusId;
}
float HardcoverBookActivity::effectiveRating() const {
  return link.pendingRating > 0 ? link.pendingRating : link.lastSyncedRating;
}
bool HardcoverBookActivity::progressUnchanged() const {
  return link.pendingProgress >= 0 ? link.pendingProgress == progressPercent
                                   : link.lastSyncedProgress == progressPercent;
}
void HardcoverBookActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finishAfterBackPress();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    handleSelection();
    return;
  }
  if (actionCount <= 0) return;
  buttonNavigator.onNext([this] {
    selectedIndex = (selectedIndex + 1) % actionCount;
    feedback = StrId::STR_NONE_OPT;
    requestUpdate();
  });
  buttonNavigator.onPrevious([this] {
    selectedIndex = (selectedIndex + actionCount - 1) % actionCount;
    feedback = StrId::STR_NONE_OPT;
    requestUpdate();
  });
}
void HardcoverBookActivity::showMemoryError() {
  LOG_ERR("HDC", "Could not allocate Hardcover child activity");
  {
    RenderLock lock(*this);
    feedback = StrId::STR_MEMORY_ERROR;
  }
  requestUpdate();
}
void HardcoverBookActivity::handleSelection() {
  if (selectedIndex < 0 || selectedIndex >= actionCount) return;
  const auto action = actions[selectedIndex];
  if (action == Status)
    selectStatus();
  else if (action == LinkBook)
    selectLinkMethod();
  else if (action == UpdateProgress) {
    if (progressUnchanged()) {
      feedback = StrId::STR_HARDCOVER_NO_CHANGE;
      requestUpdate();
    } else
      showQueueResult(HARDCOVER_LINKS.prepareBook(epubPath, title) &&
                      HARDCOVER_LINKS.queueProgress(epubPath, progressPercent));
  } else if (action == Rate) {
    const float rating = effectiveRating();
    std::vector<std::string> options;
    options.reserve(10);
    for (int halfStars = 1; halfStars <= 10; ++halfStars) {
      char label[32];
      snprintf(label, sizeof(label), tr(STR_HARDCOVER_HALF_RATING_FORMAT), halfStars / 2.0);
      options.emplace_back(label);
    }
    // The selector owns ten short labels for real half-star choices only.
    auto selector = makeUniqueNoThrow<OptionSelectionActivity>(
        renderer, mappedInput, "HardcoverRate", StrId::STR_HARDCOVER_RATE, std::move(options),
        static_cast<uint8_t>(rating > 0 ? std::clamp(static_cast<int>(rating * 2) - 1, 0, 9) : 0), true, true,
        rating > 0);
    if (!selector) {
      showMemoryError();
      return;
    }
    startActivityForResult(std::move(selector), [this](const ActivityResult& result) {
      if (result.isCancelled) return;
      const int halfStars = std::get<OptionSelectionResult>(result.data).index + 1;
      const float selected = halfStars / 2.0f;
      if (selected == effectiveRating()) {
        feedback = StrId::STR_HARDCOVER_NO_CHANGE;
        requestUpdate();
      } else
        showQueueResult(HARDCOVER_LINKS.prepareBook(epubPath, title) &&
                        HARDCOVER_LINKS.queueRating(epubPath, selected));
    });
  } else if (action == Sync) {
    sendUpdates();
  } else if (action == Pending) {
    reviewPending();
  } else if (action == Setup) {
    auto setup = makeUniqueNoThrow<HardcoverSettingsActivity>(renderer, mappedInput, true, true);
    if (!setup) {
      showMemoryError();
      return;
    }
    startActivityForResult(std::move(setup), [this](const ActivityResult& result) {
      if (std::get_if<HardcoverResult>(&result.data)) {
        sendUpdates(true);
        return;
      }
      reloadState();
      requestUpdate();
    });
  }
}
void HardcoverBookActivity::selectStatus() {
  // Only five short strings, reserved once for this selector's lifetime.
  std::vector<std::string> options;
  options.reserve(5);
  for (int status = 1; status <= 5; ++status) options.emplace_back(statusLabel(status));
  const int status = effectiveStatus();
  const bool knownStatus = status >= 1 && status <= 5;
  auto selector = makeUniqueNoThrow<OptionSelectionActivity>(
      renderer, mappedInput, "HardcoverStatus", StrId::STR_HARDCOVER_CHANGE_STATUS, std::move(options),
      static_cast<uint8_t>(knownStatus ? status - 1 : 1), true, true, knownStatus);
  if (!selector) {
    showMemoryError();
    return;
  }
  startActivityForResult(std::move(selector), [this](const ActivityResult& result) {
    if (result.isCancelled) return;
    const int chosen = std::get<OptionSelectionResult>(result.data).index + 1;
    if (chosen == effectiveStatus()) {
      feedback = StrId::STR_HARDCOVER_NO_CHANGE;
      requestUpdate();
    } else
      showQueueResult(HARDCOVER_LINKS.prepareBook(epubPath, title) && HARDCOVER_LINKS.queueStatus(epubPath, chosen));
  });
}
void HardcoverBookActivity::selectLinkMethod() {
  std::vector<std::string> options;
  options.reserve(2);
  options.emplace_back(tr(STR_HARDCOVER_AUTO_LINK));
  options.emplace_back(tr(STR_HARDCOVER_BOOK_ID));
  auto selector = makeUniqueNoThrow<OptionSelectionActivity>(renderer, mappedInput, "HardcoverLink",
                                                             StrId::STR_HARDCOVER_MATCHED_BOOK, std::move(options), 0,
                                                             true, true, false);
  if (!selector) {
    showMemoryError();
    return;
  }
  startActivityForResult(std::move(selector), [this](const ActivityResult& result) {
    if (result.isCancelled) return;
    if (std::get<OptionSelectionResult>(result.data).index == 0) {
      if (HARDCOVER_LINKS.queueLookup(epubPath, title, author, ""))
        sendUpdates();
      else
        showQueueResult(false);
      return;
    }
    auto keyboard = makeUniqueNoThrow<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_HARDCOVER_BOOK_ID),
                                                             link.bookId > 0 ? std::to_string(link.bookId) : "", 20,
                                                             InputType::Text);
    if (!keyboard) {
      showMemoryError();
      return;
    }
    startActivityForResult(std::move(keyboard), [this](const ActivityResult& input) {
      if (!input.isCancelled) handleLinkInput(std::get<KeyboardResult>(input.data).text);
    });
  });
}
void HardcoverBookActivity::handleLinkInput(const std::string& input) {
  if (looksLikeIsbn(input)) {
    if (HARDCOVER_LINKS.queueLookup(epubPath, title, author, input))
      sendUpdates();
    else
      showQueueResult(false);
    return;
  }
  if (!input.empty() && std::all_of(input.begin(), input.end(), [](char c) { return c >= '0' && c <= '9'; })) {
    const unsigned long id = std::strtoul(input.c_str(), nullptr, 10);
    if (id > 0 && id <= static_cast<unsigned long>(std::numeric_limits<int>::max())) {
      if (HARDCOVER_LINKS.queueLookup(epubPath, title, author, std::string("id:") + input))
        sendUpdates();
      else
        showQueueResult(false);
      return;
    }
  }
  feedback = StrId::STR_HARDCOVER_INVALID_LINK;
  requestUpdate();
}
void HardcoverBookActivity::sendUpdates(const bool authenticate) {
  // A manual lookup may have just been saved by the linking screen.
  reloadState();
  if (!authenticate && link.bookId <= 0 && link.pendingLookup.empty()) {
    if (!HARDCOVER_LINKS.queueLookup(epubPath, title, author, "")) {
      showQueueResult(false);
      return;
    }
  }
  setResult(HardcoverResult{authenticate});
  finish();
}

void HardcoverBookActivity::reviewPending() {
  std::vector<std::string> options;
  options.reserve(5);
  std::vector<uint8_t> fields;
  fields.reserve(5);
  char label[96];
  if (link.pendingStatusId > 0) {
    snprintf(label, sizeof(label), "%s: %s", tr(STR_HARDCOVER_CHANGE_STATUS), statusLabel(link.pendingStatusId));
    options.emplace_back(label);
    fields.push_back(0);
  }
  if (link.pendingProgress >= 0) {
    snprintf(label, sizeof(label), "%s: %d%%", tr(STR_HARDCOVER_PROGRESS), link.pendingProgress);
    options.emplace_back(label);
    fields.push_back(1);
  }
  if (link.pendingRating > 0) {
    snprintf(label, sizeof(label), "%s: %.1f", tr(STR_HARDCOVER_RATING), static_cast<double>(link.pendingRating));
    options.emplace_back(label);
    fields.push_back(2);
  }
  if (!link.pendingLookup.empty()) {
    options.emplace_back(tr(STR_HARDCOVER_LINK_BOOK));
    fields.push_back(3);
  }
  options.emplace_back(tr(STR_HARDCOVER_DISCARD_ALL));
  fields.push_back(4);
  auto selector = makeUniqueNoThrow<OptionSelectionActivity>(renderer, mappedInput, "HardcoverPending",
                                                             StrId::STR_HARDCOVER_DISCARD_PENDING, std::move(options),
                                                             0, true, true, false);
  if (!selector) {
    showMemoryError();
    return;
  }
  startActivityForResult(std::move(selector), [this, fields = std::move(fields)](const ActivityResult& result) {
    if (result.isCancelled) return;
    const auto* selected = std::get_if<OptionSelectionResult>(&result.data);
    if (!selected || selected->index >= fields.size()) return;
    const auto field = fields[selected->index];
    // ConfirmationActivity copies this bounded, cold-path prompt into its owner.
    char question[160];
    if (field == 0)
      snprintf(question, sizeof(question), tr(STR_HARDCOVER_DISCARD_STATUS), statusLabel(link.pendingStatusId));
    else if (field == 1)
      snprintf(question, sizeof(question), tr(STR_HARDCOVER_DISCARD_PROGRESS), link.pendingProgress);
    else if (field == 2)
      snprintf(question, sizeof(question), tr(STR_HARDCOVER_DISCARD_RATING), static_cast<double>(link.pendingRating));
    else
      snprintf(question, sizeof(question), "%s",
               field == 3 ? tr(STR_HARDCOVER_DISCARD_LINK) : tr(STR_HARDCOVER_DISCARD_ALL_CONFIRM));
    auto confirm =
        makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_HARDCOVER_DISCARD_PENDING), question);
    if (!confirm) {
      showMemoryError();
      return;
    }
    startActivityForResult(std::move(confirm), [this, field](const ActivityResult& response) {
      if (response.isCancelled) return;
      const bool cleared = field == 0   ? HARDCOVER_LINKS.clearPendingStatus(epubPath)
                           : field == 1 ? HARDCOVER_LINKS.clearPendingProgress(epubPath)
                           : field == 2 ? HARDCOVER_LINKS.clearPendingRating(epubPath)
                           : field == 3 ? HARDCOVER_LINKS.clearPendingLookup(epubPath)
                                        : HARDCOVER_LINKS.clearPending(epubPath);
      showQueueResult(cleared);
    });
  });
}

void HardcoverBookActivity::showQueueResult(bool queued) {
  reloadState();
  {
    RenderLock lock(*this);
    feedback = !queued ? StrId::STR_HARDCOVER_QUEUE_FAILED
                       : (hasPendingUpdate ? StrId::STR_HARDCOVER_UPDATE_QUEUED : StrId::STR_HARDCOVER_NO_CHANGE);
  }
  requestUpdate();
}
void HardcoverBookActivity::onExit() {
  Activity::onExit();
  if (auto* cache = renderer.getFontCacheManager()) cache->clearCache();
  HardcoverClient::shutdownNetwork();
}
void HardcoverBookActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  const Rect header{screen.x, screen.y + metrics.topPadding, screen.width, metrics.headerHeight};
  GUI.drawHeader(renderer, header, tr(STR_HARDCOVER));
  const int summaryHeight = std::max(metrics.tabBarHeight, renderer.getLineHeight(UI_12_FONT_ID) + 8);
  int y = header.y + header.height + metrics.verticalSpacing;
  char progress[8];
  snprintf(progress, sizeof(progress), "%d%%", std::clamp(progressPercent, 0, 100));
  GUI.drawSubHeader(renderer, Rect{screen.x, y, screen.width, summaryHeight}, title.c_str(), progress);
  y += summaryHeight;
  if (feedback != StrId::STR_NONE_OPT || hasPendingUpdate || link.bookId > 0) {
    GUI.drawSubHeader(renderer, Rect{screen.x, y, screen.width, summaryHeight},
                      I18N.get(feedback != StrId::STR_NONE_OPT ? feedback
                               : hasPendingUpdate              ? StrId::STR_HARDCOVER_UPDATE_QUEUED
                                                               : StrId::STR_HARDCOVER_SAVED_STATE));
    y += summaryHeight;
  }
  const int listTop = y + metrics.verticalSpacing;
  const int bottom = screen.y + screen.height - metrics.buttonHintsHeight - metrics.verticalSpacing;
  GUI.drawList(
      renderer, Rect{screen.x, listTop, screen.width, std::max(0, bottom - listTop)}, actionCount, selectedIndex,
      [this](int index) {
        const auto action = actions[index];
        if (action == LinkBook && link.bookId <= 0) return std::string(tr(STR_HARDCOVER_LINK_BOOK));
        if (action == UpdateProgress) {
          char label[64];
          snprintf(label, sizeof(label), tr(STR_HARDCOVER_QUEUE_CURRENT_FORMAT), progressPercent);
          return std::string(label);
        }
        return std::string(I18N.get(kMenuItems[action]));
      },
      nullptr, nullptr,
      [this](int index) {
        char value[96] = {};
        switch (actions[index]) {
          case Status:
            snprintf(value, sizeof(value), "%s%s%s", statusLabel(effectiveStatus()),
                     link.pendingStatusId > 0 ? " · " : "",
                     link.pendingStatusId > 0 ? tr(STR_HARDCOVER_LOCAL_PENDING) : "");
            break;
          case UpdateProgress:
            if (link.pendingProgress >= 0)
              snprintf(value, sizeof(value), tr(STR_HARDCOVER_QUEUED_PERCENT), link.pendingProgress);
            else if (progressUnchanged())
              snprintf(value, sizeof(value), "%s", tr(STR_HARDCOVER_NO_CHANGE));
            break;
          case Rate:
            if (effectiveRating() > 0)
              snprintf(value, sizeof(value), "%.1f*%s%s", static_cast<double>(effectiveRating()),
                       link.pendingRating > 0 ? " · " : "",
                       link.pendingRating > 0 ? tr(STR_HARDCOVER_LOCAL_PENDING) : "");
            else
              snprintf(value, sizeof(value), "%s", effectiveRating() < 0 ? tr(STR_HARDCOVER_UNKNOWN) : tr(STR_NOT_SET));
            break;
          case LinkBook:
            if (!link.remoteTitle.empty()) return link.remoteTitle;
            if (link.bookId > 0) snprintf(value, sizeof(value), "#%d", link.bookId);
            break;
          default:
            break;
        }
        return std::string(value);
      },
      true);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);
  renderer.displayBuffer();
}
