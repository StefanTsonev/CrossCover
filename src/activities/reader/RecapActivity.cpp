#include "RecapActivity.h"

#include <GfxRenderer.h>
#include <Memory.h>
#include <RecapClient.h>
#include <WiFi.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/WifiPowerSaveGuard.h"

void RecapActivity::onEnter() {
  Activity::onEnter();
  scope = Recap::scopeTitle();
  if (!scope.empty()) {
    char label[192];
    snprintf(label, sizeof(label), tr(STR_RECAP_THROUGH), scope.c_str());
    scope = label;
  }
  if (!Recap::hasRequest()) {
    message = StrId::STR_RECAP_UNSUPPORTED;
    requestUpdate();
    return;
  }
  if (Recap::loadCached(summary)) {
    requestUpdate();
    return;
  }
  if (!network) {
    auto confirmation =
        makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, "", tr(STR_RECAP_CONFIRM), true, true);
    if (!confirmation) {
      message = StrId::STR_MEMORY_ERROR;
      requestUpdate();
      return;
    }
    startActivityForResult(std::move(confirmation), [this](const ActivityResult& result) {
      if (result.isCancelled)
        finish();
      else
        silentRestartToNetwork(NetworkBootTarget::RECAP);
    });
    return;
  }
  busy = true;
  auto wifi = makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput);
  if (!wifi) {
    busy = false;
    message = StrId::STR_MEMORY_ERROR;
    requestUpdate();
    return;
  }
  startActivityForResult(std::move(wifi), [this](const ActivityResult& result) {
    if (result.isCancelled) {
      busy = false;
      retryable = true;
      message = StrId::STR_WIFI_CONN_FAILED;
      requestUpdate();
      return;
    }
    message = StrId::STR_RECAP_PREPARING;
    requestUpdateAndWait();
    uint32_t retrySeconds = 0;
    const auto status = [this, &retrySeconds]() {
      WifiPowerSaveGuard wifiPowerSaveGuard;
      return Recap::generate(
          [](void* context, size_t done, size_t total) {
            auto& activity = *static_cast<RecapActivity*>(context);
            const uint32_t now = millis();
            const unsigned percent = total ? static_cast<unsigned>(std::min(done, total) * 100 / total) : 0;
            const bool boundary = !total || done >= total || activity.lastUploadPercent == 101;
            if (!boundary && (percent == activity.lastUploadPercent || now - activity.lastUploadRefresh < 5000)) return;
            activity.lastUploadRefresh = now;
            activity.lastUploadPercent = percent;
            // Progress is independent of the rest of the screen state. Atomics
            // avoid waiting for an e-ink refresh while the socket is transmitting.
            activity.uploadStarted.store(true);
            activity.uploadDone.store(done);
            activity.uploadTotal.store(total);
            activity.requestUpdate(true);
          },
          this, &retrySeconds);
    }();
    WiFi.disconnect(false);
    WiFi.mode(WIFI_OFF);
    std::string loaded;
    const bool ready = status == "ready" && Recap::loadCached(loaded);
    {
      RenderLock lock(*this);
      busy = false;
      quotaWaitSeconds = retrySeconds;
      retryable = status == "network_error" || status == "busy" || status == "interrupted" ||
                  status == "generation_failed" || status == "service_unavailable" || status == "upload_required" ||
                  status == "upload_incomplete";
      if (ready) {
        summary = std::move(loaded);
        message = StrId::STR_RECAP_ORIGIN;
      } else if (status == "quota_daily")
        message = StrId::STR_RECAP_QUOTA_DAILY;
      else if (status == "quota_minute")
        message = StrId::STR_RECAP_QUOTA_MINUTE;
      else if (status == "budget_exhausted")
        message = StrId::STR_RECAP_QUOTA;
      else if (status == "needs_text")
        message = StrId::STR_RECAP_NEEDS_TEXT;
      else if (status == "book_too_large")
        message = StrId::STR_RECAP_BOOK_TOO_LARGE;
      else if (status == "context_too_large")
        message = StrId::STR_RECAP_CONTEXT_TOO_LARGE;
      else if (status == "unsupported_book")
        message = StrId::STR_RECAP_UNSUPPORTED;
      else if (status == "busy" || status == "interrupted")
        message = StrId::STR_RECAP_BUSY;
      else if (status == "not_configured" || status == "search_not_configured")
        message = StrId::STR_RECAP_SETUP;
      else
        message = StrId::STR_RECAP_FAILED;
    }
    if (!ready) LOG_ERR("RECAP", "Recap failed: free=%u maxAlloc=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    requestUpdate();
  });
}
void RecapActivity::loop() {
  if (busy) return;
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (network)
      silentRestartToReader(true);
    else
      finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) && network && retryable) {
    silentRestartToNetwork(NetworkBootTarget::RECAP);
  }
  if (!summary.empty()) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Down) ||
        mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      page = std::min(page + 1, pages - 1);
      requestUpdate();
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Up) ||
        mappedInput.wasReleased(MappedInputManager::Button::Left)) {
      page = std::max(0, page - 1);
      requestUpdate();
    }
  }
}
void RecapActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  GUI.drawHeader(renderer, Rect{safe.x, safe.y + metrics.topPadding, safe.width, metrics.headerHeight},
                 tr(STR_STORY_SO_FAR));
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID) + 4;
  const int width = std::max(1, safe.width - 2 * metrics.contentSidePadding);
  int y = safe.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int bottom = safe.y + safe.height - metrics.buttonHintsHeight - metrics.verticalSpacing;
  if (!scope.empty()) {
    for (const auto& line : renderer.wrappedText(UI_10_FONT_ID, scope.c_str(), width, 2)) {
      if (y + lineHeight > bottom) break;
      renderer.drawText(UI_10_FONT_ID, safe.x + metrics.contentSidePadding, y, line.c_str());
      y += lineHeight;
    }
    y += metrics.verticalSpacing;
  }
  const size_t total = uploadTotal.load();
  const StrId stage = busy ? (total                   ? StrId::STR_RECAP_UPLOADING
                              : !uploadStarted.load() ? StrId::STR_RECAP_PREPARING
                                                      : StrId::STR_RECAP_LOADING)
                           : message;
  const auto origin = renderer.wrappedText(UI_10_FONT_ID, summary.empty() ? I18N.get(stage) : tr(STR_RECAP_ORIGIN),
                                           width, summary.empty() ? 8 : 2);
  for (const auto& line : origin) {
    if (y + lineHeight > bottom) break;
    renderer.drawText(UI_10_FONT_ID, safe.x + metrics.contentSidePadding, y, line.c_str());
    y += lineHeight;
  }
  if (!busy && summary.empty() && quotaWaitSeconds && message != StrId::STR_RECAP_QUOTA_DAILY) {
    char wait[128];
    const bool minutes = quotaWaitSeconds >= 60;
    snprintf(wait, sizeof(wait), minutes ? tr(STR_RECAP_QUOTA_WAIT_MINUTES) : tr(STR_RECAP_QUOTA_WAIT_SECONDS),
             static_cast<unsigned>(minutes ? (quotaWaitSeconds + 59) / 60 : quotaWaitSeconds));
    for (const auto& line : renderer.wrappedText(UI_10_FONT_ID, wait, width, 3)) {
      if (y + lineHeight > bottom) break;
      renderer.drawText(UI_10_FONT_ID, safe.x + metrics.contentSidePadding, y, line.c_str());
      y += lineHeight;
    }
  }
  if (!summary.empty()) {
    y += metrics.verticalSpacing;
    const int perPage = std::max(1, (bottom - y - lineHeight - metrics.verticalSpacing) / lineHeight);
    if (lineWidth != width) {
      lines = renderer.wrappedText(UI_10_FONT_ID, summary.c_str(), width, 256);
      lineWidth = width;
    }
    pages = std::max(1, static_cast<int>((lines.size() + perPage - 1) / perPage));
    page = std::clamp(page, 0, pages - 1);
    for (int i = page * perPage; i < static_cast<int>(lines.size()) && i < (page + 1) * perPage; ++i) {
      renderer.drawText(UI_10_FONT_ID, safe.x + metrics.contentSidePadding, y, lines[i].c_str());
      y += lineHeight;
    }
  }
  if (busy && total) {
    char progress[96];
    snprintf(progress, sizeof(progress), tr(STR_RECAP_UPLOAD_PROGRESS),
             static_cast<unsigned>(std::min(uploadDone.load(), total) * 100 / total));
    y += metrics.verticalSpacing;
    for (const auto& line : renderer.wrappedText(UI_10_FONT_ID, progress, width, 2)) {
      if (y + lineHeight > bottom) break;
      renderer.drawText(UI_10_FONT_ID, safe.x + metrics.contentSidePadding, y, line.c_str());
      y += lineHeight;
    }
  }
  if (!summary.empty() && pages > 1) {
    char counter[32];
    snprintf(counter, sizeof(counter), tr(STR_RECAP_PAGE), page + 1, pages);
    renderer.drawText(UI_10_FONT_ID, safe.x + metrics.contentSidePadding, bottom - lineHeight, counter);
  }
  const auto labels = mappedInput.mapLabels(
      busy ? "" : tr(STR_BACK), !busy && network && retryable ? tr(STR_RETRY) : "",
      !summary.empty() && page > 0 ? tr(STR_DIR_UP) : "", !summary.empty() && page + 1 < pages ? tr(STR_DIR_DOWN) : "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
