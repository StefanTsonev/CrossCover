#include <DateFormatting.h>

#include <cstdio>

#include "I18n.h"

bool formatDateParts(char* buf, const size_t bufSize, const uint16_t year, const uint8_t month, const uint8_t day,
                     const uint8_t dateFormat, const char numericSeparator) {
  if (bufSize == 0 || month < 1 || month > 12 || day < 1 || day > 31) return false;

  static constexpr StrId monthNames[] = {
      StrId::STR_MONTH_JAN_SHORT, StrId::STR_MONTH_FEB_SHORT, StrId::STR_MONTH_MAR_SHORT, StrId::STR_MONTH_APR_SHORT,
      StrId::STR_MONTH_MAY_SHORT, StrId::STR_MONTH_JUN_SHORT, StrId::STR_MONTH_JUL_SHORT, StrId::STR_MONTH_AUG_SHORT,
      StrId::STR_MONTH_SEP_SHORT, StrId::STR_MONTH_OCT_SHORT, StrId::STR_MONTH_NOV_SHORT, StrId::STR_MONTH_DEC_SHORT};
  static constexpr StrId fullMonthNames[] = {StrId::STR_MONTH_JAN, StrId::STR_MONTH_FEB, StrId::STR_MONTH_MAR,
                                             StrId::STR_MONTH_APR, StrId::STR_MONTH_MAY, StrId::STR_MONTH_JUN,
                                             StrId::STR_MONTH_JUL, StrId::STR_MONTH_AUG, StrId::STR_MONTH_SEP,
                                             StrId::STR_MONTH_OCT, StrId::STR_MONTH_NOV, StrId::STR_MONTH_DEC};
  const char separator = numericSeparator == '.' || numericSeparator == '-' ? numericSeparator : '/';
  int length = 0;
  switch (dateFormat) {
    case 1:  // Day month year, long
      length = std::snprintf(buf, bufSize, "%02u %s %u", day, I18N.get(monthNames[month - 1]), year);
      break;
    case 2:  // Month day year, numeric
      length = std::snprintf(buf, bufSize, "%02u%c%02u%c%u", month, separator, day, separator, year);
      break;
    case 3:  // Day month year, numeric
      length = std::snprintf(buf, bufSize, "%02u%c%02u%c%u", day, separator, month, separator, year);
      break;
    case 4:  // Year month day, numeric
      length = std::snprintf(buf, bufSize, "%u%c%02u%c%02u", year, separator, month, separator, day);
      break;
    case 5:  // Month day, numeric
      length = std::snprintf(buf, bufSize, "%02u%c%02u", month, separator, day);
      break;
    case 6:  // Day month, numeric
      length = std::snprintf(buf, bufSize, "%02u%c%02u", day, separator, month);
      break;
    case 7:  // Month day, long
      length = std::snprintf(buf, bufSize, "%s %02u", I18N.get(fullMonthNames[month - 1]), day);
      break;
    case 8:  // Day month, long
      length = std::snprintf(buf, bufSize, "%02u %s", day, I18N.get(fullMonthNames[month - 1]));
      break;
    case 0:  // Month day year, long
    default:
      length = std::snprintf(buf, bufSize, "%s %02u, %u", I18N.get(monthNames[month - 1]), day, year);
      break;
  }
  return length >= 0 && static_cast<size_t>(length) < bufSize;
}
