#include <cstdio>
#include <cstdlib>

#include "HardcoverBookLinkStore.h"
#include "HardcoverClient.h"
#include "PersistableStore.h"

namespace {
void require(bool value, const char* message) {
  if (!value) {
    std::fprintf(stderr, "%s\n", message);
    std::exit(1);
  }
}
HardcoverBookLink read(const char* path = "/book.epub") {
  HardcoverBookLink link;
  require(HARDCOVER_LINKS.getLink(path, link), "Could not reload saved link");
  return link;
}
void reset() {
  HardcoverTestStorage::reset();
  require(HARDCOVER_LINKS.setLink("/book.epub", 11, "Local title", 22, 20), "Could not create link");
}
}  // namespace

int main() {
  // Existing SD files migrate without discarding queued edits.
  HardcoverTestStorage::file = R"({"links":[{"path":"/book.epub","bookId":11,"pendingProgress":42}]})";
  auto legacy = read();
  require(legacy.lastSyncedStatusId == 0 && legacy.lastSyncedRating == -1 && legacy.pendingProgress == 42,
          "Legacy state must remain unknown and preserve its pending progress");

  reset();
  require(HARDCOVER_LINKS.queueProgress("/book.epub", 42), "Could not queue progress");
  require(HARDCOVER_LINKS.queueFinish("/book.epub"), "Could not queue read status");
  require(read().pendingProgress == 42 && read().pendingStatusId == 3, "Read status must not force 100%");
  const int before = HardcoverTestStorage::writes;
  require(HARDCOVER_LINKS.queueStatus("/book.epub", 3), "Could not repeat desired status");
  require(HardcoverTestStorage::writes == before, "Repeated status must not write the SD card");

  // Failure to persist an acknowledgement must leave the whole pending action intact.
  HardcoverTestStorage::failWrite = true;
  require(!HARDCOVER_LINKS.confirmStatus("/book.epub", 3), "Injected write failure must fail acknowledgement");
  require(read().pendingStatusId == 3 && read().lastSyncedStatusId == 0, "Failed acknowledgement lost pending state");
  HardcoverTestStorage::failWrite = false;
  require(HARDCOVER_LINKS.confirmStatus("/book.epub", 3), "Could not retry acknowledgement");
  require(read().pendingStatusId == 0 && read().lastSyncedStatusId == 3, "Status acknowledgement was not atomic");

  require(HARDCOVER_LINKS.queueStatus("/book.epub", 4), "Could not queue paused status");
  require(HARDCOVER_LINKS.confirmStatus("/book.epub", 2), "Could not acknowledge older request");
  require(read().pendingStatusId == 4, "An older acknowledgement must not discard a newer choice");
  require(HARDCOVER_LINKS.queueStatus("/book.epub", 2), "Could not restore confirmed status");
  require(read().pendingStatusId == 0, "Restoring confirmed status must cancel the pending change");

  require(HARDCOVER_LINKS.confirmProgress("/book.epub", 42), "Could not confirm progress");
  require(HARDCOVER_LINKS.queueRating("/book.epub", 4), "Could not queue rating");
  HardcoverLibraryBook remote;
  remote.bookId = 11;
  remote.statusId = 2;
  remote.rating = 3;
  remote.title = "Matched title";
  remote.pages = 20;
  remote.progressPages = 8;
  remote.editionId = 22;
  require(HARDCOVER_LINKS.cacheLibrary({remote}), "Could not cache server state");
  auto cached = read();
  require(cached.pendingRating == 4 && cached.lastSyncedRating == 3, "Server refresh must preserve pending rating");
  require(cached.lastSyncedProgress == 42, "Remote page rounding must not make unchanged progress dirty");
  require(cached.remoteTitle == "Matched title", "Matched book title was not cached");
  const int cachedWrites = HardcoverTestStorage::writes;
  require(HARDCOVER_LINKS.cacheLibrary({remote}) && HardcoverTestStorage::writes == cachedWrites,
          "Unchanged server state must not rewrite the SD card");
  require(HARDCOVER_LINKS.setLink("/book.epub", 11, "Another edition", 99, 200), "Could not change edition");
  require(read().lastSyncedProgress == -1 && read().lastSyncedStatusId == 2,
          "An edition change must invalidate progress while preserving the book's status");
  require(HARDCOVER_LINKS.cacheLibrary({remote}) && read().lastSyncedProgress == -1,
          "Progress from another remote edition must not make the new edition appear synced");

  require(HARDCOVER_LINKS.setLink("/other.epub", 33, "Other"), "Could not create second book");
  require(HARDCOVER_LINKS.queueStatus("/other.epub", 5), "Could not queue second book");
  require(HARDCOVER_LINKS.confirmRating("/book.epub", 4), "Could not acknowledge current book");
  require(read("/other.epub").pendingStatusId == 5, "Current-book acknowledgement touched another book");
  std::vector<HardcoverBookLink> scoped;
  require(HARDCOVER_LINKS.getPending(scoped, "/other.epub") && scoped.size() == 1 && scoped[0].bookId == 33,
          "A current-book sync must exclude every other queued book");

  require(HARDCOVER_LINKS.setLink("/book.epub", 44, "Replacement"), "Could not relink book");
  auto replaced = read();
  require(replaced.lastSyncedStatusId == 0 && replaced.lastSyncedRating == -1 && replaced.lastSyncedProgress == -1 &&
              replaced.editionId == 0 && replaced.remoteTitle.empty(),
          "Relinking must clear the old book's confirmed identity and state");

  require(HARDCOVER_LINKS.queueLookup("/unlinked.epub", "Search title", "Author", ""), "Could not queue lookup");
  require(read("/unlinked.epub").bookId == 0 && !read("/unlinked.epub").pendingLookup.empty(),
          "Unlinked books must retain and display queued lookup state");
  require(HARDCOVER_LINKS.prepareBook("/prepared.epub", "Prepared"), "Could not prepare an unlinked book");
  require(HARDCOVER_LINKS.queueStatus("/prepared.epub", 2) && HARDCOVER_LINKS.queueProgress("/prepared.epub", 35) &&
              HARDCOVER_LINKS.queueRating("/prepared.epub", 4.5f),
          "An unlinked book must allow preparing all changes offline");
  const auto prepared = read("/prepared.epub");
  require(prepared.bookId == 0 && prepared.pendingLookup.empty() && prepared.pendingRating == 4.5f,
          "Preparation must not invent a link or round half-star ratings");
  require(!HARDCOVER_LINKS.queueRating("/prepared.epub", 4.25f), "Quarter-star ratings must be rejected");
  require(HARDCOVER_LINKS.confirmRating("/prepared.epub", 0.5f), "Could not confirm a half-star rating");
  require(read("/prepared.epub").pendingRating == 4.5f && read("/prepared.epub").lastSyncedRating == 0.5f,
          "Acknowledging an older rating must preserve the newer pending rating");
  require(HARDCOVER_LINKS.clearPending("/prepared.epub"), "Could not discard prepared changes");
  const auto discarded = read("/prepared.epub");
  require(discarded.pendingStatusId == 0 && discarded.pendingProgress == -1 && discarded.pendingRating == 0 &&
              discarded.lastSyncedRating == 0.5f,
          "Discarding pending changes must preserve confirmed half-star ratings");
  HardcoverTestStorage::file = "{broken";
  const int failedWrites = HardcoverTestStorage::writes;
  require(!HARDCOVER_LINKS.setLink("/book.epub", 77, "New"), "Corrupt storage must not be replaced");
  require(HardcoverTestStorage::writes == failedWrites, "Corrupt storage was overwritten");
  std::puts("Hardcover store regression checks passed");
}
