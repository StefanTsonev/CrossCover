#include "DictionaryActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cctype>
#include <utility>

#include "CrossPointSettings.h"
#include "DictionaryRegistry.h"
#include "StarDictReader.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

void trimLookupWord(std::string& text) {
  const auto replaceAll = [&text](const char* from, const char* to) {
    const std::string source(from);
    const std::string replacement(to);
    size_t pos = 0;
    while ((pos = text.find(source, pos)) != std::string::npos) {
      text.replace(pos, source.size(), replacement);
      pos += replacement.size();
    }
  };
  replaceAll("\xE2\x80\x98", "'");
  replaceAll("\xE2\x80\x99", "'");
  size_t start = 0;
  size_t end = text.size();
  while (start < end && (std::isspace(static_cast<unsigned char>(text[start])) ||
                         std::ispunct(static_cast<unsigned char>(text[start])))) {
    ++start;
  }
  while (end > start && (std::isspace(static_cast<unsigned char>(text[end - 1])) ||
                         std::ispunct(static_cast<unsigned char>(text[end - 1])))) {
    --end;
  }
  text = text.substr(start, end - start);
}

void replaceUtf8(std::string& text, const char* from, const char* to) {
  const std::string source(from);
  const std::string replacement(to);
  size_t pos = 0;
  while ((pos = text.find(source, pos)) != std::string::npos) {
    text.replace(pos, source.size(), replacement);
    pos += replacement.size();
  }
}

void makePhoneticsReadable(std::string& text) {
  // The built-in UI fonts do not contain the full IPA block. Use compact ASCII
  // approximations instead of allowing the renderer to display replacement '?'.
  replaceUtf8(text, "ɔ", "o");
  replaceUtf8(text, "ə", "uh");
  replaceUtf8(text, "ɪ", "i");
  replaceUtf8(text, "ʊ", "u");
  replaceUtf8(text, "ɛ", "e");
  replaceUtf8(text, "æ", "ae");
  replaceUtf8(text, "ɑ", "a");
  replaceUtf8(text, "ʌ", "u");
  replaceUtf8(text, "ɜ", "er");
  replaceUtf8(text, "ɒ", "o");
  replaceUtf8(text, "ɹ", "r");
  replaceUtf8(text, "ʃ", "sh");
  replaceUtf8(text, "ʒ", "zh");
  replaceUtf8(text, "θ", "th");
  replaceUtf8(text, "ð", "dh");
  replaceUtf8(text, "ŋ", "ng");
  replaceUtf8(text, "ː", ":");
}

void formatDefinition(std::string& text) {
  replaceUtf8(text, "&nbsp;", " ");
  replaceUtf8(text, "&amp;", "&");
  replaceUtf8(text, "&lt;", "<");
  replaceUtf8(text, "&gt;", ">");
  replaceUtf8(text, "&quot;", "\"");

  // StarDict definitions are often HTML-ish. The renderer's word wrapper only
  // treats ordinary spaces as separators, so convert tags and all whitespace.
  std::string formatted;
  formatted.reserve(text.size());
  bool inTag = false;
  bool pendingSpace = false;
  for (const unsigned char c : text) {
    if (c == '<') {
      inTag = true;
      pendingSpace = true;
      continue;
    }
    if (inTag) {
      if (c == '>') inTag = false;
      continue;
    }
    if (c == '\n' || c == '\r' || c == '\t' || std::isspace(c)) {
      pendingSpace = true;
      continue;
    }
    if (pendingSpace && !formatted.empty()) formatted.push_back(' ');
    pendingSpace = false;
    formatted.push_back(static_cast<char>(c));
  }
  while (!formatted.empty() && formatted.back() == ' ') formatted.pop_back();
  text = std::move(formatted);
}

}  // namespace

DictionaryActivity::DictionaryActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string selectedWord)
    : Activity("Dictionary", renderer, mappedInput), selectedWord(std::move(selectedWord)) {}

void DictionaryActivity::onEnter() {
  Activity::onEnter();
  trimLookupWord(selectedWord);
  status = StrId::STR_DICTIONARY_LOADING;
  requestUpdateAndWait();
  std::vector<DictionaryEntry> dictionaries;
  DictionaryRegistry::discover(dictionaries);
  if (dictionaries.empty()) {
    status = StrId::STR_DICTIONARY_MISSING;
    lookupComplete = true;
    requestUpdate();
    return;
  }
  auto selected = std::find_if(dictionaries.begin(), dictionaries.end(),
                               [](const DictionaryEntry& item) { return item.name == SETTINGS.dictionary; });
  if (selected == dictionaries.end()) selected = dictionaries.begin();
  const std::string dictionaryBase = "/.crosspoint/dictionaries/" + selected->name + "/" + selected->stem;
  StarDictReader reader(dictionaryBase);
  switch (reader.lookup(selectedWord, definition)) {
    case StarDictReader::Result::Found:
      break;
    case StarDictReader::Result::Missing:
      status = StrId::STR_DICTIONARY_MISSING;
      break;
    case StarDictReader::Result::Invalid:
      status = StrId::STR_DICTIONARY_INVALID;
      break;
    default:
      status = StrId::STR_DICTIONARY_NOT_FOUND;
      break;
  }
  formatDefinition(definition);
  makePhoneticsReadable(definition);
  lookupComplete = true;
  requestUpdate();
}

void DictionaryActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) finish();
}

void DictionaryActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth() - 2 * metrics.contentSidePadding;
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight},
                 tr(STR_DICTIONARY));

  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const auto wordLines = renderer.wrappedText(UI_12_FONT_ID, selectedWord.c_str(), width, 2, EpdFontFamily::BOLD);
  for (const auto& line : wordLines) {
    renderer.drawText(UI_12_FONT_ID, metrics.contentSidePadding, y, line.c_str(), true, EpdFontFamily::BOLD);
    y += renderer.getLineHeight(UI_12_FONT_ID);
  }
  y += metrics.verticalSpacing;

  const char* body = !lookupComplete || definition.empty() ? I18n::getInstance().get(status) : definition.c_str();
  const int availableHeight = renderer.getScreenHeight() - y - metrics.buttonHintsHeight - metrics.contentSidePadding;
  const int maxLines = std::max(1, availableHeight / lineHeight);
  const auto lines = renderer.wrappedText(UI_10_FONT_ID, body, width, maxLines);
  for (const auto& line : lines) {
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, y, line.c_str());
    y += lineHeight;
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
