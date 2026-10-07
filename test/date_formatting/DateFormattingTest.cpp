#include <DateFormatting.h>
#include <I18n.h>

#include <cassert>
#include <cstring>

int main() {
  char date[32];
  I18N.setLanguage(Language::EN);
  assert(formatDateParts(date, sizeof(date), 2026, 10, 3, 0));
  assert(std::strcmp(date, "Oct 03, 2026") == 0);
  I18N.setLanguage(Language::DE);
  assert(formatDateParts(date, sizeof(date), 2026, 10, 3, 0));
  assert(std::strcmp(date, "Okt. 03, 2026") == 0);
  assert(formatDateParts(date, sizeof(date), 2026, 10, 3, 8));
  assert(std::strcmp(date, "03 Oktober") == 0);
  I18N.setLanguage(Language::FR);
  assert(formatDateParts(date, sizeof(date), 2026, 2, 3, 7));
  assert(std::strcmp(date, "février 03") == 0);
  assert(formatDateParts(date, sizeof(date), 2026, 2, 3, 4, '-'));
  assert(std::strcmp(date, "2026-02-03") == 0);
  assert(formatDateParts(date, sizeof(date), 2026, 2, 3, 3, '.'));
  assert(std::strcmp(date, "03.02.2026") == 0);
  assert(!formatDateParts(date, 4, 2026, 2, 3, 7));
  assert(!formatDateParts(date, sizeof(date), 2026, 0, 3, 0));
  assert(!formatDateParts(date, sizeof(date), 2026, 13, 3, 0));
  // All translated dates, including multibyte month names, fit the UI's date buffer.
  for (unsigned language = 0; language < static_cast<unsigned>(Language::_COUNT); ++language) {
    I18N.setLanguage(static_cast<Language>(language));
    for (unsigned month = 1; month <= 12; ++month) {
      for (unsigned format = 0; format <= 8; ++format) {
        assert(formatDateParts(date, sizeof(date), 2026, month, 28, format));
        assert(std::strstr(date, "???") == nullptr);
      }
    }
  }
}
