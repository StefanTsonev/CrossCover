#include <cstdio>

#include "SilentRestart.h"
#include "activities/reader/EpubReaderMenuModel.h"

int main() {
  for (const bool button : {false, true}) {
    ReaderDrawerAvailability availability{};
    availability.buttonDevice = button;
    const auto catalog = makeReaderDrawerCatalog(availability);
    const auto& location = catalog[static_cast<size_t>(ReaderDrawerTab::Location)];
    int recap = -1, hardcover = -1, count = 0;
    for (int i = 0; i < location.count; ++i) {
      if (location.items[i] == ReaderDrawerCatalogItem::StorySoFar) {
        recap = i;
        ++count;
      }
      if (location.items[i] == ReaderDrawerCatalogItem::Hardcover) hardcover = i;
    }
    if (count != 1 || hardcover < 0 || recap != hardcover + 1) {
      std::fprintf(stderr, "Recap must occur once immediately after Hardcover on touch and button devices\n");
      return 1;
    }
  }
  if (!isNetworkBootTargetValue(static_cast<uint32_t>(NetworkBootTarget::RECAP))) return 1;
  std::puts("Recap menu and network-boot checks passed");
}
